import type { Clock } from '../../ports/clock';
import type { BandwidthChannel, Command, DampingChannel, LoopMode, Reply } from '../protocol/types';
import type { AppError } from '../app-error';
import { guardError } from '../app-error';
import type { Result } from '../../shared/result';
import { Emitter, type Unsubscribe } from '../../shared/subscribe';
import { emptySnapshot, type DeviceSnapshot } from './device';
import { isFaultState } from './faults';
import { STALE_AFTER_FAILURES } from '../protocol/limits';

export type Priority = 'user' | 'poll';

/** 总线抽象（由 `core/scheduler.ts` 实现，测试里可以换成假实现）。 */
export interface CommandBus {
  submit(addr: number, cmd: Command, priority: Priority): Promise<Result<Reply, AppError>>;
}

/** 桥接固件里真正检查 `g_debug_confirmed` 的命令（见 `app_main.cpp` 各 case）。 */
const DEBUG_GATED: ReadonlySet<Command['kind']> = new Set<Command['kind']>([
  'calibrate',
  'writeBaud',
  'defaults',
  'save',
  'writeOverCurrent',
  'zeroOffset',
]);

export class DeviceSession {
  readonly addr: number;

  private state: DeviceSnapshot;
  private readonly bus: CommandBus;
  private readonly clock: Clock;
  private readonly changes = new Emitter<DeviceSnapshot>();
  private debugUnlocked = false;

  constructor(addr: number, bus: CommandBus, clock: Clock, name?: string) {
    this.addr = addr;
    this.bus = bus;
    this.clock = clock;
    this.state = emptySnapshot(addr, name);
  }

  snapshot(): DeviceSnapshot {
    return this.state;
  }

  get isDebugUnlocked(): boolean {
    return this.debugUnlocked;
  }

  subscribe(cb: (snapshot: DeviceSnapshot) => void): Unsubscribe {
    return this.changes.subscribe(cb);
  }

  /** 命令排队由 Scheduler 负责；这里只做用例级门禁。 */
  async exec(cmd: Command, priority: Priority = 'user'): Promise<Result<Reply, AppError>> {
    if (DEBUG_GATED.has(cmd.kind) && !this.debugUnlocked) {
      return { ok: false, error: guardError('debug-locked', '先读取设备信息（INFO）解锁调试命令') };
    }
    return this.bus.submit(this.addr, cmd, priority);
  }

  /** 由 Scheduler 在认领应答后调用。未知回包只更新时间戳，不污染快照。 */
  applyReply(reply: Reply): void {
    const patch: Partial<DeviceSnapshot> = {
      link: 'online',
      failures: 0,
      lastSeenMs: this.clock.now(),
    };

    switch (reply.kind) {
      case 'feedback':
        patch.iqA = reply.iqA;
        patch.speedRadS = reply.speedRadS;
        patch.posRad = reply.posRad;
        break;

      case 'vbus':
        patch.vbusV = reply.volts;
        break;

      case 'sysreg':
        patch.sysreg = {
          raw: reply.value,
          state: reply.state,
          warn: reply.warn,
          fault: reply.fault,
        };
        if (isFaultState(reply.state)) patch.enabled = false;
        break;

      case 'info':
        this.debugUnlocked = true;
        patch.config = {
          ...this.state.config,
          firmwareVersion: reply.firmwareVersion,
          chipId: reply.chipId,
          deviceId: reply.deviceId,
        };
        break;

      case 'deviceAddr':
        patch.config = { ...this.state.config, deviceAddr: reply.addr };
        break;

      case 'ack':
        this.applyAck(reply.cmd, reply.args, patch);
        break;

      case 'error':
        if (reply.code === 'DEBUG') this.debugUnlocked = false;
        break;

      case 'pong':
      case 'calibStarted':
      case 'unknown':
        break;
    }

    this.commit(patch);
  }

  private applyAck(cmd: string, args: readonly string[], patch: Partial<DeviceSnapshot>): void {
    switch (cmd) {
      case 'ENABLE':
        patch.enabled = true;
        break;
      case 'DISABLE':
        patch.enabled = false;
        break;
      case 'IQ':
      case 'SPD':
      case 'POS': {
        const mode: LoopMode = cmd === 'IQ' ? 'cur' : cmd === 'SPD' ? 'spd' : 'pos';
        patch.mode = mode;
        patch.target = numberArg(args, 0, this.state.target);
        break;
      }
      case 'MODE':
        patch.mode = modeFromArg(args[0]) ?? this.state.mode;
        break;
      case 'BW': {
        const which = (args[0] ?? '').toUpperCase();
        const hz = numberArg(args, 1, Number.NaN);
        if (isBandwidthChannel(which) && Number.isFinite(hz)) {
          patch.config = {
            ...this.state.config,
            bandwidthHz: { ...this.state.config.bandwidthHz, [which]: hz },
          };
        }
        break;
      }
      case 'OVC': {
        const amps = numberArg(args, 0, Number.NaN);
        const ms = numberArg(args, 1, Number.NaN);
        patch.config = { ...this.state.config };
        if (Number.isFinite(amps)) patch.config.overCurrentA = amps;
        if (Number.isFinite(ms)) patch.config.overCurrentMs = ms;
        break;
      }
      case 'ZETA': {
        const which = (args[0] ?? '').toUpperCase();
        const value = numberArg(args, 1, Number.NaN);
        if (isDampingChannel(which) && Number.isFinite(value)) {
          patch.config = {
            ...this.state.config,
            zeta: { ...this.state.config.zeta, [which]: value },
          };
        }
        break;
      }
      case 'DEFAULTS':
        patch.config = {};
        break;
      case 'BAUD': {
        const baud = numberArg(args, 0, Number.NaN);
        if (Number.isFinite(baud)) patch.config = { ...this.state.config, baud };
        break;
      }
      case 'DEFAULTS':
        patch.config = {};
        break;
      case 'INFO':
      case 'DADDR':
      case 'CLEAR':
      case 'SAVE':
      case 'ADDR':
      case 'ZOFF':
        break;
      default:
        break;
    }
  }

  noteSuccess(): void {
    this.commit({ link: 'online', failures: 0, lastSeenMs: this.clock.now() });
  }

  noteFailure(): void {
    const failures = this.state.failures + 1;
    this.commit({
      failures,
      link: failures >= STALE_AFTER_FAILURES ? 'stale' : this.state.link,
    });
  }

  markOffline(): void {
    this.commit({ link: 'offline', failures: 0 });
  }

  private commit(patch: Partial<DeviceSnapshot>): void {
    this.state = { ...this.state, ...patch };
    this.changes.emit(this.state);
  }
}

function numberArg(args: readonly string[], index: number, fallback: number): number {
  const token = args[index];
  if (token === undefined) return fallback;
  const value = Number(token);
  return Number.isFinite(value) ? value : fallback;
}

function modeFromArg(token: string | undefined): LoopMode | null {
  const upper = (token ?? '').toUpperCase();
  if (upper === 'CUR' || upper === '0') return 'cur';
  if (upper === 'SPD' || upper === '1') return 'spd';
  if (upper === 'POS' || upper === '2') return 'pos';
  return null;
}

function isBandwidthChannel(value: string): value is BandwidthChannel {
  return value === 'CUR' || value === 'SPD' || value === 'POS' || value === 'ESO' || value === 'AB';
}

function isDampingChannel(value: string): value is DampingChannel {
  return value === 'SPD' || value === 'AB';
}
