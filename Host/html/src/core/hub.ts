import type { Clock } from '../ports/clock';
import type { AppError } from './app-error';
import { DeviceSession, type CommandBus } from './domain/session';
import type { DeviceSnapshot, LinkState } from './domain/device';
import type { Result } from '../shared/result';
import { Emitter, type Unsubscribe } from '../shared/subscribe';

export interface SessionBus extends CommandBus {
  register(session: DeviceSession): void;
  unregister(addr: number): void;
}

export type HubEvent =
  | { kind: 'device-added'; addr: number }
  | { kind: 'device-removed'; addr: number }
  | { kind: 'device-updated'; addr: number; snapshot: DeviceSnapshot }
  | { kind: 'link'; addr: number; link: LinkState }
  | { kind: 'fault'; addr: number; fault: number; warn: number };

export interface BroadcastFailure {
  addr: number;
  error: AppError;
}

/** 多台设备注册表：不碰协议细节，只负责"有哪些设备"和"批量动作"。 */
export class DeviceHub {
  private readonly bus: SessionBus;
  private readonly clock: Clock;
  private readonly sessions = new Map<number, DeviceSession>();
  private readonly sessionUnsubs = new Map<number, Unsubscribe>();
  private readonly lastFlags = new Map<number, string>();
  private readonly events = new Emitter<HubEvent>();

  constructor(bus: SessionBus, clock: Clock) {
    this.bus = bus;
    this.clock = clock;
  }

  add(addr: number, name?: string): DeviceSession {
    const existing = this.sessions.get(addr);
    if (existing !== undefined) return existing;

    const session = new DeviceSession(addr, this.bus, this.clock, name);
    this.sessions.set(addr, session);
    this.bus.register(session);
    this.sessionUnsubs.set(
      addr,
      session.subscribe((snapshot) => this.handleSnapshot(addr, snapshot)),
    );
    this.events.emit({ kind: 'device-added', addr });
    return session;
  }

  remove(addr: number): void {
    const session = this.sessions.get(addr);
    if (session === undefined) return;
    this.sessionUnsubs.get(addr)?.();
    this.sessionUnsubs.delete(addr);
    this.bus.unregister(addr);
    this.sessions.delete(addr);
    this.lastFlags.delete(addr);
    this.events.emit({ kind: 'device-removed', addr });
  }

  list(): DeviceSession[] {
    return [...this.sessions.values()].sort((a, b) => a.addr - b.addr);
  }

  get(addr: number): DeviceSession | undefined {
    return this.sessions.get(addr);
  }

  subscribe(cb: (event: HubEvent) => void): Unsubscribe {
    return this.events.subscribe(cb);
  }

  /** 批量动作串行执行，失败逐台汇报（不能因为它们共享一条总线就并发）。 */
  async broadcast(
    fn: (session: DeviceSession) => Promise<Result<unknown, AppError>>,
  ): Promise<BroadcastFailure[]> {
    const failures: BroadcastFailure[] = [];
    for (const session of this.list()) {
      const result = await fn(session);
      if (!result.ok) failures.push({ addr: session.addr, error: result.error });
    }
    return failures;
  }

  dispose(): void {
    for (const unsub of this.sessionUnsubs.values()) unsub();
    this.sessionUnsubs.clear();
    this.sessions.clear();
    this.events.clear();
  }

  private handleSnapshot(addr: number, snapshot: DeviceSnapshot): void {
    this.events.emit({ kind: 'device-updated', addr, snapshot });
    this.events.emit({ kind: 'link', addr, link: snapshot.link });

    // 故障位只在"变化"时通知：否则 50 Hz 轮询会把状态条刷爆
    const flags = `${snapshot.sysreg.fault}:${snapshot.sysreg.warn}`;
    if (this.lastFlags.get(addr) === flags) return;
    this.lastFlags.set(addr, flags);
    if (snapshot.sysreg.fault !== 0 || snapshot.sysreg.warn !== 0) {
      this.events.emit({
        kind: 'fault',
        addr,
        fault: snapshot.sysreg.fault,
        warn: snapshot.sysreg.warn,
      });
    }
  }
}
