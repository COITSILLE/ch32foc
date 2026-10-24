import type { Clock } from '../ports/clock';
import type { Logger } from '../ports/logger';
import type {
  OpenOptions,
  Transport,
  TransportError,
  TransportKind,
  TransportState,
} from '../ports/transport';
import type { Store } from './store';
import type { AppState, LogDirection } from './state';
import { LOG_CAPACITY } from './state';
import type { AppError } from '../core/app-error';
import { fromTransportError, guardError, transportError } from '../core/app-error';
import { BusScheduler } from '../core/scheduler';
import { DeviceHub, type HubEvent } from '../core/hub';
import type { DeviceSession } from '../core/domain/session';
import type { BandwidthChannel, Command, DampingChannel, LoopMode, Reply } from '../core/protocol/types';
import { MAX_ADDR, MIN_ADDR, SOFT_RANGES, STALE_AFTER_FAILURES } from '../core/protocol/limits';
import { decodeSysReg } from '../core/domain/faults';
import type { DeviceSnapshot } from '../core/domain/device';
import { errorText, errorTone } from './errors';
import { isFaultState, stateLabel } from '../core/domain/faults';
import { AMPLITUDE_PRESETS, UNIT_LABEL } from '../core/domain/units';
import { clamp } from '../shared/fmt';
import { Emitter, type Unsubscribe } from '../shared/subscribe';
import type { Result } from '../shared/result';

export interface ActionsDeps {
  store: Store<AppState>;
  clock: Clock;
  logger: Logger;
  createTransport(): Transport;
}

export interface ParamWrites {
  bandwidth?: Partial<Record<BandwidthChannel, number>>;
  zeta?: Partial<Record<DampingChannel, number>>;
  overCurrent?: { amps: number; ms: number };
  baud?: number;
}

const PATTERN_TICK_MS = 200;
const SQUARE_HALF_PERIOD_S = 1.2;
const SINE_HZ = 0.25;

/**
 * 用例层：UI 只调这里的"动词"，不碰 transport / parse / units。
 *
 * 这里保留了 `web/index.html` 已验证过的时序与边界：
 *  - 连接后先 PING，再开轮询
 *  - 只读地址、写参数不落盘、校准期间不重发
 *  - 故障态/清故障后补读 SYSREG
 */
export class AppActions {
  private readonly deps: ActionsDeps;
  private transport: Transport | null = null;
  private scheduler: BusScheduler | null = null;
  private hub: DeviceHub | null = null;
  private unsubs: Unsubscribe[] = [];
  private readonly sessionUnsubs = new Map<number, Unsubscribe>();
  private patternTimer: number | null = null;
  private logSeq = 0;
  private toastSeq = 0;
  private readonly busy = new Emitter<string | null>();

  constructor(deps: ActionsDeps) {
    this.deps = deps;
  }

  get hubRef(): DeviceHub | null {
    return this.hub;
  }

  get schedulerRef(): BusScheduler | null {
    return this.scheduler;
  }

  onBusy(cb: (label: string | null) => void): Unsubscribe {
    return this.busy.subscribe(cb);
  }

  // -------------------------------------------------------------- 连接

  async connect(): Promise<void> {
    const state = this.deps.store.getState();
    if (state.phase !== 'idle') return;

    this.deps.store.setState({ phase: 'connecting', statusText: '正在连接…', lastError: null });

    const transport = this.deps.createTransport();
    const baudRate = state.baudRate;
    try {
      await transport.open({ baudRate });
    } catch (e) {
      const message = e instanceof Error ? e.message : String(e);
      const error = /不支持/.test(message)
        ? transportError('unsupported', message)
        : transportError('open-failed', message);
      this.fail(error);
      this.deps.store.setState({ phase: 'idle', statusText: '连接失败' });
      return;
    }

    const logged = new LoggingTransport(transport, (dir, text) => {
      this.pushLog(dir, text, this.scheduler?.stats.inFlightAddr ?? null);
    });
    this.transport = logged;

    const scheduler = new BusScheduler(logged, this.deps.clock);
    const hub = new DeviceHub(scheduler, this.deps.clock);
    this.scheduler = scheduler;
    this.hub = hub;

    this.unsubs.push(hub.subscribe((evt) => this.onHubEvent(evt)));
    this.unsubs.push(scheduler.onTransportError((e) => this.onLinkError(e)));

    this.deps.store.setState({
      phase: 'connected',
      connection: { label: transport.label, baudRate },
      statusText: '已连接',
      startedAtMs: this.deps.clock.now(),
      debug: { unlocked: false, target: 0 },
    });

    // 先按地址 0 接入，其余设备手动添加（桥接的 ADDR 由 Scheduler 自动补）
    this.addDevice(0);

    const ping = await scheduler.submit(0, { kind: 'ping' }, 'user');
    if (!ping.ok) this.fail(ping.error);

    scheduler.setPolling(true);
    this.deps.store.setState({ pollEnabled: true, pollHz: scheduler.perDeviceHz() });
  }

  async disconnect(): Promise<void> {
    this.clearPatterns();
    if (this.scheduler !== null) {
      this.scheduler.setPolling(false);
      this.scheduler.dispose();
    }
    for (const unsub of this.unsubs.splice(0, this.unsubs.length)) unsub();
    for (const unsub of this.sessionUnsubs.values()) unsub();
    this.sessionUnsubs.clear();
    this.hub?.dispose();
    this.hub = null;
    this.scheduler = null;

    const transport = this.transport;
    this.transport = null;
    if (transport !== null) {
      await transport.close();
    }

    this.deps.store.setState({
      phase: 'idle',
      connection: null,
      devices: {},
      pollEnabled: false,
      pollHz: 0,
      patterns: {},
      lastStep: null,
      debug: { unlocked: false, target: 0 },
      busyOperation: null,
      statusText: '未连接',
      startedAtMs: null,
    });
    this.pushLog('sys', '已断开连接');
  }

  setBaudRate(baudRate: number): void {
    this.deps.store.setState({ baudRate: Math.trunc(baudRate) });
  }

  setPolling(enabled: boolean): void {
    this.scheduler?.setPolling(enabled);
    this.deps.store.setState({ pollEnabled: enabled });
  }

  // -------------------------------------------------------------- 设备

  addDevice(addr: number): void {
    if (this.hub === null) {
      this.showToast('请先连接链路', 'warn');
      return;
    }
    if (addr < MIN_ADDR || addr > MAX_ADDR || !Number.isInteger(addr)) {
      this.showToast(`设备地址必须在 ${MIN_ADDR}..${MAX_ADDR} 之间`, 'warn');
      return;
    }
    if (this.hub.get(addr) !== undefined) {
      this.selectDevice(addr);
      this.showToast(`地址 ${addr} 已在列表中`, 'info');
      return;
    }

    const session = this.hub.add(addr);
    this.wireSession(session);
    this.deps.store.setState((state) => ({
      activeAddr: state.devices[state.activeAddr] === undefined ? addr : state.activeAddr,
      devices: { ...state.devices, [addr]: session.snapshot() },
    }));
    this.pushLog('sys', `已添加设备 ${addr}（下一条命令前会自动发 ADDR ${addr}）`);
  }

  removeDevice(addr: number): void {
    if (this.hub === null) return;
    this.sessionUnsubs.get(addr)?.();
    this.sessionUnsubs.delete(addr);
    this.hub.remove(addr);
    this.deps.store.setState((state) => {
      const devices = { ...state.devices };
      delete devices[addr];
      const remaining = Object.keys(devices).map(Number).sort((a, b) => a - b);
      return {
        devices,
        activeAddr: state.activeAddr === addr ? (remaining[0] ?? 0) : state.activeAddr,
      };
    });
    this.pushLog('sys', `已移除设备 ${addr}`);
  }

  selectDevice(addr: number): void {
    if (this.deps.store.getState().devices[addr] === undefined) return;
    this.deps.store.setState({ activeAddr: addr });
  }

  /** 调试页可以从另一台设备读参数，所以单独记一个目标地址。 */
  selectDebugTarget(addr: number): void {
    const session = this.hub?.get(addr);
    this.deps.store.setState({
      debug: { unlocked: session?.isDebugUnlocked ?? false, target: addr },
    });
  }

  // -------------------------------------------------------------- 控制

  async setMode(addr: number, mode: LoopMode): Promise<void> {
    await this.run(addr, { kind: 'setMode', mode });
  }

  async enable(addr: number, on: boolean): Promise<void> {
    const command: Command = on ? { kind: 'enable' } : { kind: 'disable' };
    const result = await this.run(addr, command);
    if (!result.ok && result.error.kind === 'device' && result.error.code === 'FAULT') {
      await this.readSysreg(addr);
    }
  }

  async setTarget(addr: number, value: number): Promise<void> {
    const session = this.hub?.get(addr);
    if (session === undefined) return;
    await this.run(addr, { kind: 'setTarget', mode: session.snapshot().mode, value });
  }

  /** 演示用阶跃：切速度环 → 下目标 → 记录起点（供指标计算）。 */
  async step(addr: number, to: number): Promise<void> {
    const session = this.hub?.get(addr);
    if (session === undefined) return;
    const before = session.snapshot();
    if (before.mode !== 'spd') {
      const switched = await this.run(addr, { kind: 'setMode', mode: 'spd' });
      if (!switched.ok) return;
    }
    const result = await this.run(addr, { kind: 'setTarget', mode: 'spd', value: to });
    if (!result.ok) return;
    this.deps.store.setState({
      lastStep: { addr, from: before.speedRadS, to, tMs: this.deps.clock.now() },
    });
  }

  /**
   * 开始/重启一台设备的波形跟踪。**不打断其它设备**——
   * 每台各有一条独立的时间基准，靠同一个 tick 一起发目标。
   *
   * 幅度与单位跟着**当前闭环模式**：速度环 rad/s、位置环 rad（以起点为中心）、电流环 A。
   * 位置环特意取"开始时的位置"作中心，避免从当前位置一下跳到 0 rad。
   * 不传幅度时用该模式的默认值。
   */
  playPattern(addr: number, kind: 'square' | 'sine', amplitude?: number): void {
    const session = this.hub?.get(addr);
    if (session === undefined) return;
    const snapshot = session.snapshot();
    const mode = snapshot.mode;
    const amp = amplitude ?? AMPLITUDE_PRESETS[mode];
    const center = mode === 'pos' ? snapshot.posRad : 0;

    this.deps.store.setState((state) => ({
      patterns: {
        ...state.patterns,
        [addr]: { kind, mode, amplitude: amp, center, startedAtMs: this.deps.clock.now() },
      },
    }));
    this.pushLog(
      'sys',
      `设备 ${addr} 开始${kind === 'square' ? '方波' : '正弦'}跟踪 ${modeLabel(mode)} ±${amp} ${patternUnit(mode)}`,
      addr,
    );
    this.ensurePatternTimer();
  }

  /** 不传地址 = 停全部；只停一台时把它稳在当前位置。 */
  stopPattern(addr?: number): void {
    const patterns = this.deps.store.getState().patterns;
    const targets = addr === undefined ? Object.keys(patterns).map(Number) : [addr];
    if (targets.length === 0) return;

    this.deps.store.setState((state) => {
      const next = { ...state.patterns };
      for (const target of targets) delete next[target];
      return { patterns: next };
    });
    if (Object.keys(this.deps.store.getState().patterns).length === 0) this.stopPatternTimer();

    for (const target of targets) {
      const pattern = patterns[target];
      if (pattern === undefined) continue;
      const session = this.hub?.get(target);
      // 位置环不能直接发 0（会让电机跑回原点），发当前位置相当于"停在这"
      const value = pattern.mode === 'pos' ? (session?.snapshot().posRad ?? pattern.center) : 0;
      this.pushLog('sys', `设备 ${target} 停止跟踪`, target);
      void this.run(target, { kind: 'setTarget', mode: pattern.mode, value });
    }
  }

  private ensurePatternTimer(): void {
    if (this.patternTimer !== null) return;
    this.patternTimer = this.deps.clock.setInterval(this.patternTick, PATTERN_TICK_MS);
  }

  private stopPatternTimer(): void {
    if (this.patternTimer === null) return;
    this.deps.clock.clearInterval(this.patternTimer);
    this.patternTimer = null;
  }

  private readonly patternTick = (): void => {
    const patterns = this.deps.store.getState().patterns;
    const addrs = Object.keys(patterns).map(Number);
    if (addrs.length === 0) {
      this.stopPatternTimer();
      return;
    }

    const now = this.deps.clock.now();
    for (const addr of addrs) {
      const pattern = patterns[addr];
      if (pattern === undefined) continue;
      const elapsed = (now - pattern.startedAtMs) / 1000;
      const wave =
        pattern.kind === 'square'
          ? Math.floor(elapsed / SQUARE_HALF_PERIOD_S) % 2 === 0
            ? 1
            : -1
          : Math.sin(2 * Math.PI * SINE_HZ * elapsed);
      const raw = pattern.center + pattern.amplitude * wave;
      // 位置给 3 位小数（rad），速度/电流给 1 位：小幅度时四舍五入到整数会变成台阶
      const factor = pattern.mode === 'pos' ? 1000 : 10;
      const value = Math.round(raw * factor) / factor;
      void this.run(addr, { kind: 'setTarget', mode: pattern.mode, value }).then((result) => {
        // 该台掉线/被拒就单独停掉，别让失败每拍刷屏
        if (!result.ok) this.stopPattern(addr);
      });
    }
  };

  /** 全停（不发 0，用于断链时清状态）。 */
  private clearPatterns(): void {
    this.stopPatternTimer();
    this.deps.store.setState({ patterns: {} });
  }

  /** 全部急停：停掉所有跟踪，再逐台断使能。 */
  async stopAll(): Promise<void> {
    this.stopPattern();
    const hub = this.hub;
    if (hub === null) return;
    const failures = await hub.broadcast(async (session) => session.exec({ kind: 'disable' }));
    for (const failure of failures) this.fail(failure.error);
    const count = hub.list().length - failures.length;
    this.setStatus(`已停止 ${count} 台${failures.length > 0 ? `，${failures.length} 台失败` : ''}`);
  }

  async clearFault(addr: number): Promise<void> {
    // 固件的 CLEAR_FAULT 只在 FAULT 态执行，其它状态直接回 FAULT 帧（变成误导性的 ERR FAULT）。
    // 有可信快照时先自己判断，省一次必定被拒的往返。
    const snapshot = this.hub?.get(addr)?.snapshot();
    if (
      snapshot !== undefined &&
      snapshot.link === 'online' &&
      snapshot.lastSeenMs > 0 &&
      !isFaultState(snapshot.sysreg.state)
    ) {
      const label = stateLabel(snapshot.sysreg.state);
      this.showToast(`当前不在故障态（${label}），无需清除`, 'info');
      this.setStatus(`当前不在故障态（${label}），无需清故障`);
      return;
    }
    const result = await this.run(addr, { kind: 'clearFault' });
    if (result.ok) {
      this.showToast('故障已清除', 'info');
      await this.readSysreg(addr);
      const after = this.hub?.get(addr)?.snapshot();
      if (after !== undefined) {
        this.setStatus(`故障已清除，当前状态：${stateLabel(after.sysreg.state)}`);
      }
    }
  }

  async readSysreg(addr: number): Promise<void> {
    await this.run(addr, { kind: 'readSysreg' });
  }

  async ping(addr: number): Promise<void> {
    await this.run(addr, { kind: 'ping' });
  }

  // -------------------------------------------------------------- 调试

  async readInfo(addr: number): Promise<void> {
    const result = await this.run(addr, { kind: 'info' });
    const session = this.hub?.get(addr);
    const unlocked = session?.isDebugUnlocked ?? false;
    this.deps.store.setState({ debug: { unlocked, target: addr } });
    if (result.ok) this.setStatus('已进入 DEBUG 模式');
  }

  async readDeviceAddr(addr: number): Promise<void> {
    await this.run(addr, { kind: 'readDeviceAddr' });
  }

  async calibrate(addr: number): Promise<void> {
    this.setBusy('校准中（约 1.5 s）…');
    try {
      await this.run(addr, { kind: 'calibrate' });
    } finally {
      this.setBusy(null);
    }
  }

  /** 把当前连续角记成新的零点偏移（`ZOFF`）；只写 RAM，需再 `SAVE`。 */
  async zeroOffset(addr: number): Promise<void> {
    this.setBusy('重算零点偏移…');
    try {
      const result = await this.run(addr, { kind: 'zeroOffset' });
      if (result.ok) this.setStatus('零点偏移已更新（需要「保存到 Flash」才掉电保留）');
    } finally {
      this.setBusy(null);
    }
  }

  async writeParams(addr: number, params: ParamWrites): Promise<void> {
    const commands: Command[] = [];
    if (params.bandwidth !== undefined) {
      for (const [which, hz] of Object.entries(params.bandwidth)) {
        if (hz === undefined) continue;
        commands.push({ kind: 'writeBandwidth', which: which as BandwidthChannel, hz });
      }
    }
    if (params.overCurrent !== undefined) {
      commands.push({
        kind: 'writeOverCurrent',
        amps: params.overCurrent.amps,
        ms: params.overCurrent.ms,
      });
    }
    if (params.zeta !== undefined) {
      for (const [which, value] of Object.entries(params.zeta)) {
        if (value === undefined) continue;
        commands.push({ kind: 'writeZeta', which: which as DampingChannel, value });
      }
    }
    if (params.baud !== undefined) {
      commands.push({ kind: 'writeBaud', baud: params.baud });
    }

    for (const command of commands) {
      const result = await this.run(addr, command);
      if (!result.ok) return;
    }
    this.setStatus('参数已写入 RAM（需要「保存到 Flash」才掉电保留）');
  }

  async writeDefaults(addr: number): Promise<void> {
    const result = await this.run(addr, { kind: 'defaults' });
    if (result.ok) this.setStatus('已恢复默认参数（仍需保存到 Flash）');
  }

  async save(addr: number): Promise<void> {
    this.setBusy('保存到 Flash…');
    try {
      const result = await this.run(addr, { kind: 'save' });
      if (result.ok) this.setStatus('已保存到 Flash');
    } finally {
      this.setBusy(null);
    }
  }

  // -------------------------------------------------------------- 视图状态

  setChartMode(mode: AppState['chartMode']): void {
    this.deps.store.setState({ chartMode: mode });
  }

  setChartWindow(sec: AppState['chartWindowSec']): void {
    this.deps.store.setState({ chartWindowSec: sec });
  }

  setLogVisible(visible: boolean): void {
    this.deps.store.setState({ logVisible: visible });
  }

  setLogPaused(paused: boolean): void {
    this.deps.store.setState({ logPaused: paused });
  }

  clearLog(): void {
    this.deps.store.setState({ log: [] });
  }

  dismissToast(): void {
    this.deps.store.setState({ toast: null });
  }

  // -------------------------------------------------------------- 内部

  /** 统一的命令执行 + 错误上报 + DEBUG 状态同步。 */
  private async run(addr: number, command: Command): Promise<Result<Reply, AppError>> {
    const session = this.hub?.get(addr);
    if (session === undefined) {
      const error = guardError('not-connected', `设备 ${addr} 未连接`);
      this.fail(error, command);
      return { ok: false, error };
    }
    const result = await session.exec(command);
    if (!result.ok) {
      this.fail(result.error, command);
    } else {
      this.deps.store.setState({ lastError: null, lastErrorText: null, statusText: displayReply(result.value) });
    }
    if (isDebugCommand(command)) {
      this.deps.store.setState({ debug: { unlocked: session.isDebugUnlocked, target: addr } });
    }
    return result;
  }

  private wireSession(session: DeviceSession): void {
    // 每台单独记 unsub：移除设备后必须解绑，否则迟到的回包会把设备"复活"
    this.sessionUnsubs.get(session.addr)?.();
    this.sessionUnsubs.set(
      session.addr,
      session.subscribe((snapshot) => {
        this.deps.store.setState((state) => ({ devices: { ...state.devices, [snapshot.addr]: snapshot } }));
        this.syncPollHz();
      }),
    );
  }

  private syncPollHz(): void {
    const scheduler = this.scheduler;
    if (scheduler === null) return;
    const hz = scheduler.perDeviceHz();
    if (hz !== this.deps.store.getState().pollHz) {
      this.deps.store.setState({ pollHz: hz });
    }
  }

  private onHubEvent(event: HubEvent): void {
    if (event.kind === 'fault') {
      const decoded = decodeSysReg(0, event.warn, event.fault);
      const text = [
        `设备 ${event.addr}`,
        decoded.faults.length > 0 ? `故障：${decoded.faults.join('/')}` : '',
        decoded.warnings.length > 0 ? `告警：${decoded.warnings.join('/')}` : '',
      ]
        .filter(Boolean)
        .join(' ');
      this.pushLog('sys', text, event.addr);
      this.showToast(text, 'warn');
      return;
    }
    if (event.kind === 'link' && event.link === 'stale') {
      const text = `设备 ${event.addr} 连续 ${STALE_AFTER_FAILURES} 次无应答，已标记为 stale`;
      this.pushLog('sys', text, event.addr);
      this.showToast(text, 'error');
    }
  }

  private onLinkError(error: TransportError): void {
    this.pushLog('err', error.message);
    this.fail(fromTransportError(error));
    this.deps.store.setState({ pollEnabled: false });
  }

  private fail(error: AppError, command?: Command): void {
    const device = error.kind === 'device' || error.kind === 'timeout'
      ? this.deps.store.getState().devices[error.addr]
      : undefined;
    const text = errorText(error, {
      ...(command === undefined ? {} : { command: command.kind }),
      ...(device === undefined ? {} : { stateLabel: stateLabel(device.sysreg.state) }),
    });
    this.deps.store.setState({ lastError: error, lastErrorText: text, statusText: text });
    this.pushLog('err', text);
    this.deps.logger.warn(text);
    this.showToast(text, errorTone(error));
  }

  private setStatus(text: string): void {
    this.deps.store.setState({ statusText: text });
  }

  private setBusy(label: string | null): void {
    this.deps.store.setState({ busyOperation: label });
    this.busy.emit(label);
  }

  private showToast(text: string, tone: 'info' | 'warn' | 'error'): void {
    this.toastSeq += 1;
    this.deps.store.setState({ toast: { id: this.toastSeq, text, tone } });
  }

  private pushLog(dir: LogDirection, text: string, addr: number | null = null): void {
    this.logSeq += 1;
    const entry = { id: this.logSeq, tMs: this.deps.clock.now(), dir, addr, text };
    this.deps.store.setState((state) => {
      const log = state.log.length >= LOG_CAPACITY ? state.log.slice(1) : state.log;
      return { log: [...log, entry] };
    });
  }
}

function modeLabel(mode: LoopMode): string {
  return mode === 'cur' ? '电流环' : mode === 'spd' ? '速度环' : '位置环';
}

/** 波形幅度的单位跟着模式走。 */
function patternUnit(mode: LoopMode): string {
  return mode === 'cur' ? UNIT_LABEL.iqA : mode === 'spd' ? UNIT_LABEL.speedRadS : UNIT_LABEL.posRad;
}

function isDebugCommand(command: Command): boolean {
  return (
    command.kind === 'info' ||
    command.kind === 'calibrate' ||
    command.kind === 'readDeviceAddr' ||
    command.kind === 'writeBaud' ||
    command.kind === 'writeOverCurrent' ||
    command.kind === 'zeroOffset' ||
    command.kind === 'defaults' ||
    command.kind === 'save'
  );
}

function displayReply(reply: Reply): string {
  switch (reply.kind) {
    case 'feedback':
      return `FB ${reply.iqA.toFixed(2)} A / ${reply.speedRadS.toFixed(1)} ${UNIT_LABEL.speedRadS}`;
    case 'vbus':
      return `母线 ${reply.volts.toFixed(3)} V`;
    case 'sysreg': {
      const decoded = decodeSysReg(reply.state, reply.warn, reply.fault);
      return `状态 ${decoded.stateLabel}`;
    }
    case 'ack':
      return reply.raw;
    case 'info':
      return '已读取设备信息';
    case 'deviceAddr':
      return `设备地址 ${reply.addr}`;
    case 'calibStarted':
      return '校准中…';
    case 'pong':
      return 'PONG';
    case 'error':
      return reply.raw;
    case 'unknown':
      return reply.raw;
  }
}

/** 给 UI 用的量程提示（不是固件限幅）。 */
export function softLimit(kind: keyof typeof SOFT_RANGES): { min: number; max: number } {
  return SOFT_RANGES[kind];
}

export function clampTarget(value: number, kind: keyof typeof SOFT_RANGES): number {
  const range = SOFT_RANGES[kind];
  return clamp(value, range.min, range.max);
}

export function deviceSummary(snapshot: DeviceSnapshot): string {
  return `${snapshot.speedRadS.toFixed(1)} ${UNIT_LABEL.speedRadS} · ${snapshot.iqA.toFixed(2)} A`;
}

/** 包装一层只负责记日志的 Transport：协议逻辑仍然只有一份。 */
class LoggingTransport implements Transport {
  readonly kind: TransportKind;
  private readonly lines = new Emitter<string>();
  private readonly errors = new Emitter<TransportError>();
  private readonly innerUnsubs: Unsubscribe[];

  constructor(
    private readonly inner: Transport,
    private readonly sink: (dir: LogDirection, text: string) => void,
  ) {
    this.kind = inner.kind;
    this.innerUnsubs = [
      inner.onLine((line) => {
        this.sink('rx', line);
        this.lines.emit(line);
      }),
      inner.onError((e) => {
        this.sink('err', e.message);
        this.errors.emit(e);
      }),
    ];
  }

  get state(): TransportState {
    return this.inner.state;
  }

  get label(): string {
    return this.inner.label;
  }

  open(opts: OpenOptions): Promise<void> {
    return this.inner.open(opts);
  }

  async close(): Promise<void> {
    await this.inner.close();
    for (const unsub of this.innerUnsubs.splice(0, this.innerUnsubs.length)) unsub();
  }

  async write(line: string): Promise<void> {
    await this.inner.write(line);
    this.sink('tx', line);
  }

  onLine(cb: (line: string) => void): Unsubscribe {
    return this.lines.subscribe(cb);
  }

  onError(cb: (e: TransportError) => void): Unsubscribe {
    return this.errors.subscribe(cb);
  }
}
