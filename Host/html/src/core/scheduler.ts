import type { Clock } from '../ports/clock';
import type { Transport, TransportError } from '../ports/transport';
import type { AppError } from './app-error';
import { deviceError, fromTransportError, guardError, timeoutError, transportError } from './app-error';
import type { Result } from '../shared/result';
import { err, ok } from '../shared/result';
import { encode } from './protocol/commands';
import { parse } from './protocol/parse';
import type { Command, Reply } from './protocol/types';
import {
  LONG_OPERATION_TIMEOUT_MS,
  POLL_BACKOFF_MS,
  POLL_MAX_HZ,
  POLL_MIN_HZ,
  POLL_TOTAL_HZ,
  REPLY_TIMEOUT_MS,
  SYSREG_DIV,
  VBUS_DIV,
} from './protocol/limits';
import type { CommandBus, DeviceSession, Priority } from './domain/session';
import { clamp } from '../shared/fmt';
import { Emitter, type Unsubscribe } from '../shared/subscribe';

export interface SchedulerStats {
  tx: number;
  timeouts: number;
  queueDepth: number;
  inFlightAddr: number | null;
  perDeviceHz: number;
  polling: boolean;
}

interface QueueItem {
  addr: number;
  cmd: Command;
  priority: Priority;
  resolve: (result: Result<Reply, AppError>) => void;
}

interface PendingReply {
  resolve: (result: Result<Reply, AppError>) => void;
  timer: number;
}

export interface SchedulerOptions {
  replyTimeoutMs?: number;
  longOperationTimeoutMs?: number;
  /** 总线往返预算（次/秒），用于按设备数分频。 */
  totalHz?: number;
}

/**
 * 总线仲裁：保证**同一时刻只有一条命令在飞**。
 *
 * 关键约束（来自固件）：
 *  - 桥接层是请求-应答模型，128 B 环形缓冲，并发写会堆积并串到旧数据
 *  - ASCII 命令不带地址，地址由 `ADDR n` 改写桥接的目标 tag，
 *    所以换设备时每条命令前要先补一条 `ADDR n`（内部完成，不占用户队列优先级）
 */
export class BusScheduler implements CommandBus {
  private readonly transport: Transport;
  private readonly clock: Clock;
  private readonly sessions = new Map<number, DeviceSession>();
  private readonly queue: QueueItem[] = [];
  private readonly counters = new Map<number, { sys: number; vbus: number }>();
  private readonly backoffUntil = new Map<number, number>();
  private readonly unsubs: Unsubscribe[] = [];
  private readonly replyTimeoutMs: number;
  private readonly longOperationTimeoutMs: number;
  private readonly totalHz: number;
  private readonly errors = new Emitter<TransportError>();

  private pending: PendingReply | null = null;
  private inFlightAddr: number | null = null;
  private draining = false;
  private currentAddr: number | null = null;
  private polling = false;
  private pollTimer: number | null = null;
  private txCount = 0;
  private timeoutCount = 0;
  private disposed = false;

  constructor(transport: Transport, clock: Clock, options: SchedulerOptions = {}) {
    this.transport = transport;
    this.clock = clock;
    this.replyTimeoutMs = options.replyTimeoutMs ?? REPLY_TIMEOUT_MS;
    this.longOperationTimeoutMs = options.longOperationTimeoutMs ?? LONG_OPERATION_TIMEOUT_MS;
    this.totalHz = options.totalHz ?? POLL_TOTAL_HZ;

    this.unsubs.push(this.transport.onLine((line) => this.handleLine(line)));
    this.unsubs.push(this.transport.onError((e) => this.handleTransportError(e)));
  }

  register(session: DeviceSession): void {
    this.sessions.set(session.addr, session);
    this.counters.set(session.addr, { sys: 0, vbus: 0 });
    if (this.polling) this.restartPollTimer();
  }

  unregister(addr: number): void {
    this.sessions.delete(addr);
    this.counters.delete(addr);
    if (this.polling) this.restartPollTimer();
  }

  list(): DeviceSession[] {
    return [...this.sessions.values()];
  }

  onTransportError(cb: (e: TransportError) => void): Unsubscribe {
    return this.errors.subscribe(cb);
  }

  get stats(): SchedulerStats {
    return {
      tx: this.txCount,
      timeouts: this.timeoutCount,
      queueDepth: this.queue.length,
      inFlightAddr: this.inFlightAddr,
      perDeviceHz: this.perDeviceHz(),
      polling: this.polling,
    };
  }

  /** 轮询频率按设备数动态分配：`clamp(floor(220 / N), 10, 50)`。 */
  perDeviceHz(): number {
    const n = Math.max(1, this.sessions.size);
    return clamp(Math.floor(this.totalHz / n), POLL_MIN_HZ, POLL_MAX_HZ);
  }

  submit(addr: number, cmd: Command, priority: Priority): Promise<Result<Reply, AppError>> {
    if (this.disposed) {
      return Promise.resolve(err(transportError('closed', 'scheduler disposed')));
    }
    if (!this.sessions.has(addr)) {
      return Promise.resolve(err(guardError('not-connected', `设备 ${addr} 未注册`)));
    }
    return new Promise<Result<Reply, AppError>>((resolve) => {
      this.queue.push({ addr, cmd, priority, resolve });
      void this.drain();
    });
  }

  setPolling(enabled: boolean): void {
    if (this.polling === enabled) return;
    this.polling = enabled;
    if (enabled) {
      this.restartPollTimer();
    } else {
      this.stopPollTimer();
    }
  }

  dispose(): void {
    this.disposed = true;
    this.setPolling(false);
    for (const unsub of this.unsubs) unsub();
    this.unsubs.length = 0;
    this.resolvePending(transportError('closed'));
    const drained = this.queue.splice(0, this.queue.length);
    for (const item of drained) {
      item.resolve(err(transportError('closed')));
    }
  }

  // ---------------------------------------------------------------- 调度

  private async drain(): Promise<void> {
    if (this.draining) return;
    this.draining = true;
    try {
      for (;;) {
        const item = this.dequeue();
        if (item === undefined) break;
        this.inFlightAddr = item.addr;
        let result: Result<Reply, AppError>;
        try {
          result = await this.execute(item);
        } finally {
          this.inFlightAddr = null;
        }
        item.resolve(result);
      }
    } finally {
      this.draining = false;
    }
  }

  /** 用户命令永远插到轮询之前；同优先级保持 FIFO。 */
  private dequeue(): QueueItem | undefined {
    const userIndex = this.queue.findIndex((item) => item.priority === 'user');
    const index = userIndex >= 0 ? userIndex : 0;
    return this.queue.splice(index, 1)[0];
  }

  private async execute(item: QueueItem): Promise<Result<Reply, AppError>> {
    const session = this.sessions.get(item.addr);
    if (session === undefined) {
      return err(guardError('not-connected', `设备 ${item.addr} 未注册`));
    }
    if (this.transport.state !== 'open') {
      return err(transportError('closed'));
    }

    if (this.currentAddr !== item.addr) {
      const switched = await this.sendAndAwait(`ADDR ${item.addr}`, this.replyTimeoutMs);
      if (!switched.ok) {
        session.noteFailure();
        this.notePollFailure(item);
        return switched;
      }
      this.currentAddr = item.addr;
    }

    const timeout = this.timeoutFor(item.cmd);
    let result = await this.sendAndAwait(encode(item.cmd), timeout);

    // CALIB 先回 `START CALIB`，约 1.1 s 后才有最终回包；期间不能重发
    if (result.ok && result.value.kind === 'calibStarted') {
      result = await this.awaitReply(this.longOperationTimeoutMs);
    }

    if (!result.ok) {
      session.noteFailure();
      this.notePollFailure(item);
      return result;
    }

    this.backoffUntil.delete(item.addr);
    const reply = result.value;

    // 桥接回 ERR TIMEOUT 说明这个地址上的板子没应答：对"这台设备"而言就是失败
    if (reply.kind === 'error' && reply.code === 'TIMEOUT') {
      session.noteFailure();
      this.notePollFailure(item);
      return err(deviceError(reply.code, item.addr, reply.raw));
    }

    session.applyReply(reply);
    session.noteSuccess();
    if (reply.kind === 'error') {
      return err(deviceError(reply.code, item.addr, reply.raw));
    }
    return result;
  }

  private timeoutFor(cmd: Command): number {
    return cmd.kind === 'calibrate' || cmd.kind === 'save'
      ? this.longOperationTimeoutMs
      : this.replyTimeoutMs;
  }

  /**
   * 轮询失败后退避 1 s：失联设备每拍都发一次 300 ms 超时会把总线占死，
   * 其他设备就没机会回包了（"单台掉线只降级该台"）。
   */
  private notePollFailure(item: QueueItem): void {
    if (item.priority !== 'poll') return;
    this.backoffUntil.set(item.addr, this.clock.now() + POLL_BACKOFF_MS);
  }

  private async sendAndAwait(line: string, timeoutMs: number): Promise<Result<Reply, AppError>> {
    // 先挂上"等回包"，再写出去：写得快、回得更快时（仿真/回放）也不会漏掉应答
    const waiting = this.awaitReply(timeoutMs);
    const written = await this.writeLine(line);
    if (!written.ok) {
      this.cancelPending();
      return written;
    }
    return waiting;
  }

  private cancelPending(): void {
    const pending = this.pending;
    if (pending === null) return;
    this.pending = null;
    this.clock.clearTimeout(pending.timer);
    pending.resolve(err(transportError('write-failed', '命令未发出')));
  }

  private async writeLine(line: string): Promise<Result<true, AppError>> {
    try {
      await this.transport.write(line);
      this.txCount += 1;
      return ok(true);
    } catch (e) {
      const message = e instanceof Error ? e.message : String(e);
      return err(transportError('write-failed', message));
    }
  }

  private awaitReply(timeoutMs: number): Promise<Result<Reply, AppError>> {
    return new Promise<Result<Reply, AppError>>((resolve) => {
      const timer = this.clock.setTimeout(() => {
        if (this.pending === null) return;
        this.pending = null;
        this.timeoutCount += 1;
        resolve(err(timeoutError(this.inFlightAddr ?? -1)));
      }, timeoutMs);
      this.pending = { resolve, timer };
    });
  }

  private handleLine(line: string): void {
    const reply = parse(line);
    const pending = this.pending;
    if (pending === null) {
      // 没有请求在飞时收到的行（上电 banner / 手动发送）：丢弃，交给 transport 订阅者记日志
      return;
    }
    this.pending = null;
    this.clock.clearTimeout(pending.timer);
    pending.resolve(ok(reply));
  }

  private resolvePending(error: AppError): void {
    const pending = this.pending;
    if (pending === null) return;
    this.pending = null;
    this.clock.clearTimeout(pending.timer);
    pending.resolve(err(error));
  }

  private handleTransportError(e: TransportError): void {
    this.resolvePending(fromTransportError(e));
    for (const session of this.sessions.values()) {
      session.markOffline();
    }
    this.setPolling(false);
    this.errors.emit(e);
  }

  // ---------------------------------------------------------------- 轮询

  private restartPollTimer(): void {
    this.stopPollTimer();
    const interval = Math.max(10, Math.round(1000 / this.perDeviceHz()));
    this.pollTimer = this.clock.setInterval(() => {
      this.pollTick();
    }, interval);
  }

  private stopPollTimer(): void {
    if (this.pollTimer !== null) {
      this.clock.clearInterval(this.pollTimer);
      this.pollTimer = null;
    }
  }

  private pollTick(): void {
    if (!this.polling || this.transport.state !== 'open') return;
    const now = this.clock.now();
    for (const addr of this.sessions.keys()) {
      if ((this.backoffUntil.get(addr) ?? 0) > now) continue;
      // 每个设备只允许一条轮询命令在队列里，否则总线饱和时会越积越多
      if (this.hasPending(addr)) continue;
      const cmd = this.nextPollCommand(addr);
      void this.submit(addr, cmd, 'poll');
    }
  }

  /** 与 `web/index.html` 一致：SYSREG 每 5 拍、VBUS 每 20 拍，其余读 FB。 */
  private nextPollCommand(addr: number): Command {
    const counter = this.counters.get(addr) ?? { sys: 0, vbus: 0 };
    this.counters.set(addr, counter);
    counter.sys += 1;
    if (counter.sys >= SYSREG_DIV) {
      counter.sys = 0;
      return { kind: 'readSysreg' };
    }
    counter.vbus += 1;
    if (counter.vbus >= VBUS_DIV) {
      counter.vbus = 0;
      return { kind: 'readVbus' };
    }
    return { kind: 'readFeedback' };
  }

  private hasPending(addr: number): boolean {
    return this.inFlightAddr === addr || this.queue.some((item) => item.addr === addr);
  }
}
