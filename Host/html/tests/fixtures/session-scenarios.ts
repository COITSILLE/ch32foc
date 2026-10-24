import type { Clock } from '../../src/ports/clock';
import type { CommandBus, Priority } from '../../src/core/domain/session';
import type { Command, Reply } from '../../src/core/protocol/types';
import type { AppError } from '../../src/core/app-error';
import type { Result } from '../../src/shared/result';
import { err, ok } from '../../src/shared/result';

interface Timer {
  id: number;
  at: number;
  fn: () => void;
  interval: number | null;
}

/** 假时钟：超时、退避、轮询分频都靠它驱动，测试不睡真实时间。 */
export class FakeClock implements Clock {
  private current = 0;
  private nextId = 1;
  private timers: Timer[] = [];

  now(): number {
    return this.current;
  }

  setTimeout(fn: () => void, ms: number): number {
    return this.add(fn, ms, null);
  }

  clearTimeout(id: number): void {
    this.timers = this.timers.filter((timer) => timer.id !== id);
  }

  setInterval(fn: () => void, ms: number): number {
    return this.add(fn, ms, ms);
  }

  clearInterval(id: number): void {
    this.clearTimeout(id);
  }

  /** 推进时间并运行到期定时器（按时间顺序），每步后让出微任务。 */
  async advance(ms: number): Promise<void> {
    const target = this.current + ms;
    for (;;) {
      await flushMicrotasks();
      const due = this.timers
        .filter((timer) => timer.at <= target)
        .sort((a, b) => a.at - b.at)[0];
      if (due === undefined) break;
      this.current = due.at;
      if (due.interval === null) {
        this.timers = this.timers.filter((timer) => timer.id !== due.id);
      } else {
        due.at += due.interval;
      }
      due.fn();
      await flushMicrotasks();
    }
    this.current = target;
    await flushMicrotasks();
  }

  private add(fn: () => void, ms: number, interval: number | null): number {
    const id = this.nextId;
    this.nextId += 1;
    this.timers.push({ id, at: this.current + Math.max(0, ms), fn, interval });
    return id;
  }
}

/** 让出若干轮微任务：drain 的 await 链只依赖 promise，不需要真实定时器。 */
export async function flushMicrotasks(): Promise<void> {
  for (let i = 0; i < 30; i += 1) {
    await Promise.resolve();
  }
}

/** 假总线：按剧本应答，用来测 DeviceSession 的状态机，不碰调度器。 */
export class ScriptedBus implements CommandBus {
  readonly calls: { addr: number; cmd: Command; priority: Priority }[] = [];
  private readonly queue: Reply[] = [];

  reply(...replies: Reply[]): void {
    this.queue.push(...replies);
  }

  fail(error: AppError): void {
    this.failures.push(error);
  }

  private readonly failures: AppError[] = [];

  async submit(addr: number, cmd: Command, priority: Priority): Promise<Result<Reply, AppError>> {
    this.calls.push({ addr, cmd, priority });
    const failure = this.failures.shift();
    if (failure !== undefined) return err(failure);
    const reply = this.queue.shift();
    if (reply === undefined) return err({ kind: 'timeout', addr });
    return ok(reply);
  }
}
