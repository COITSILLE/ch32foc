import { describe, expect, it } from 'vitest';
import { BusScheduler } from '../../src/core/scheduler';
import { DeviceSession } from '../../src/core/domain/session';
import type { Clock } from '../../src/ports/clock';
import type {
  OpenOptions,
  Transport,
  TransportError,
  TransportKind,
  TransportState,
} from '../../src/ports/transport';
import { Emitter, type Unsubscribe } from '../../src/shared/subscribe';
import { FakeClock, flushMicrotasks as flush } from '../fixtures/session-scenarios';

class FakeTransport implements Transport {
  readonly kind: TransportKind = 'replay';
  readonly written: string[] = [];
  auto = false;
  /** 这些地址不回包，用来模拟"单台掉线"。 */
  readonly silent = new Set<number>();
  /** 覆盖自动回包内容（例如统一回 `ERR TIMEOUT`）。 */
  autoReplyOverride: ((line: string) => string) | null = null;
  private currentAddr = 0;
  private stateValue: TransportState = 'closed';
  private readonly lines = new Emitter<string>();
  private readonly errors = new Emitter<TransportError>();

  constructor(private readonly clock: Clock) {}

  get state(): TransportState {
    return this.stateValue;
  }

  get label(): string {
    return 'fake';
  }

  async open(_opts: OpenOptions): Promise<void> {
    this.stateValue = 'open';
  }

  async close(): Promise<void> {
    this.stateValue = 'closed';
  }

  async write(line: string): Promise<void> {
    this.written.push(line);
    const [head = '', arg = ''] = line.split(' ');
    if (head === 'ADDR') this.currentAddr = Number(arg);
    if (!this.auto || this.silent.has(this.currentAddr)) return;
    this.clock.setTimeout(() => {
      this.emit(this.autoReplyOverride?.(line) ?? autoReply(line));
    }, 2);
  }

  onLine(cb: (line: string) => void): Unsubscribe {
    return this.lines.subscribe(cb);
  }

  onError(cb: (e: TransportError) => void): Unsubscribe {
    return this.errors.subscribe(cb);
  }

  emit(line: string): void {
    this.lines.emit(line);
  }

  emitError(error: TransportError): void {
    this.errors.emit(error);
  }
}

function autoReply(line: string): string {
  const [head = ''] = line.split(' ');
  switch (head) {
    case 'ADDR':
      return `OK ADDR ${line.split(' ')[1]}`;
    case 'FB':
      return 'FB 0.100 10.000 0.0000';
    case 'VBUS':
      return 'VBUS 24.000';
    case 'SYSREG':
      return 'SYSREG 0x00010000 1 0x00000000 0x00000000';
    case 'PING':
      return 'PONG';
    default:
      return `OK ${head}`;
  }
}

async function setup(count = 1): Promise<{
  clock: FakeClock;
  transport: FakeTransport;
  scheduler: BusScheduler;
  sessions: DeviceSession[];
}> {
  const clock = new FakeClock();
  const transport = new FakeTransport(clock);
  await transport.open({ baudRate: 115200 });
  const scheduler = new BusScheduler(transport, clock);
  const sessions: DeviceSession[] = [];
  for (let addr = 0; addr < count; addr += 1) {
    const session = new DeviceSession(addr, scheduler, clock);
    scheduler.register(session);
    sessions.push(session);
  }
  return { clock, transport, scheduler, sessions };
}

describe('scheduler — 单飞与地址切换', () => {
  it('首条命令前先补 ADDR（桥接目标地址未知）', async () => {
    const { transport, scheduler } = await setup();
    const pending = scheduler.submit(0, { kind: 'readFeedback' }, 'user');
    await flush();
    expect(transport.written).toEqual(['ADDR 0']);

    transport.emit('OK ADDR 0');
    await flush();
    expect(transport.written).toEqual(['ADDR 0', 'FB']);

    transport.emit('FB 0.100 10.000 0.0000');
    const result = await pending;
    expect(result.ok).toBe(true);
    scheduler.dispose();
  });

  it('同地址后续命令不再发 ADDR', async () => {
    const { clock, transport, scheduler } = await setup();
    transport.auto = true;

    const first = scheduler.submit(0, { kind: 'readFeedback' }, 'user');
    await clock.advance(10);
    const second = scheduler.submit(0, { kind: 'readVbus' }, 'user');
    await clock.advance(10);
    await Promise.all([first, second]);

    expect(transport.written).toEqual(['ADDR 0', 'FB', 'VBUS']);
    scheduler.dispose();
  });

  it('换地址时必须先切换到目标 tag', async () => {
    const { clock, transport, scheduler } = await setup(2);
    transport.auto = true;

    const first = scheduler.submit(0, { kind: 'readFeedback' }, 'user');
    await clock.advance(10);
    const second = scheduler.submit(1, { kind: 'readFeedback' }, 'user');
    await clock.advance(10);
    await Promise.all([first, second]);

    expect(transport.written).toEqual(['ADDR 0', 'FB', 'ADDR 1', 'FB']);
    scheduler.dispose();
  });

  it('同一时刻只有一条命令在飞', async () => {
    const { transport, scheduler } = await setup();
    void scheduler.submit(0, { kind: 'readFeedback' }, 'user');
    void scheduler.submit(0, { kind: 'readVbus' }, 'user');
    await flush();

    // 第一条还在等回包，第二条不能写出去
    expect(transport.written).toEqual(['ADDR 0']);
    scheduler.dispose();
  });

  it('用户命令插队到轮询之前', async () => {
    const { transport, scheduler } = await setup();
    const poll1 = scheduler.submit(0, { kind: 'readFeedback' }, 'poll');
    await flush();
    const poll2 = scheduler.submit(0, { kind: 'readVbus' }, 'poll');
    const user = scheduler.submit(0, { kind: 'readSysreg' }, 'user');

    transport.emit('OK ADDR 0');
    await flush();
    transport.emit('FB 0.1 1.0 0.0');
    await flush();

    expect(transport.written[2]).toBe('SYSREG');

    transport.emit('SYSREG 0x00010000 1 0x00000000 0x00000000');
    await flush();
    transport.emit('VBUS 24.000');
    await flush();
    await Promise.all([poll1, poll2, user]);
    scheduler.dispose();
  });
});

describe('scheduler — 超时与降级', () => {
  it('300 ms 无回包按超时处理', async () => {
    const { clock, transport, scheduler, sessions } = await setup();
    const pending = scheduler.submit(0, { kind: 'readFeedback' }, 'user');
    await flush();
    await clock.advance(300);

    const result = await pending;
    expect(result).toMatchObject({ ok: false, error: { kind: 'timeout' } });
    expect(sessions[0]?.snapshot().failures).toBe(1);
    expect(transport.written).toEqual(['ADDR 0']);
    scheduler.dispose();
  });

  it('连续 3 次超时后设备变 stale', async () => {
    const { clock, scheduler, sessions } = await setup();
    for (let i = 0; i < 3; i += 1) {
      const pending = scheduler.submit(0, { kind: 'readFeedback' }, 'user');
      await flush();
      await clock.advance(300);
      await pending;
    }
    expect(sessions[0]?.snapshot().link).toBe('stale');
    scheduler.dispose();
  });

  it('链路错误让所有设备离线并停止轮询', async () => {
    const { transport, scheduler, sessions } = await setup(2);
    scheduler.setPolling(true);
    transport.emitError({ code: 'closed', message: '串口已拔出' });
    await flush();

    expect(sessions.map((s) => s.snapshot().link)).toEqual(['offline', 'offline']);
    expect(scheduler.stats.polling).toBe(false);
    scheduler.dispose();
  });
});

describe('scheduler — 轮询分频', () => {  it('perDeviceHz = clamp(floor(220/N), 10, 50)', async () => {
    const expectations: [number, number][] = [
      [1, 50],
      [2, 50],
      [4, 50],
      [8, 27],
    ];
    for (const [count, hz] of expectations) {
      const { scheduler } = await setup(count);
      expect(scheduler.perDeviceHz()).toBe(hz);
      scheduler.dispose();
    }
  });

  it('单台失联进入 1 s 退避，不拖死其他设备', async () => {
    const { clock, transport, scheduler, sessions } = await setup(2);
    transport.auto = true;
    transport.silent.add(0);
    scheduler.setPolling(true);
    await clock.advance(3000);
    scheduler.setPolling(false);
    await flush();

    expect(sessions[0]?.snapshot().failures).toBeGreaterThanOrEqual(3);
    expect(sessions[0]?.snapshot().link).toBe('stale');
    expect(sessions[1]?.snapshot().link).toBe('online');
    expect(sessions[1]?.snapshot().speedRadS).toBe(10);

    const attemptsToDead = transport.written.filter((line) => line === 'ADDR 0').length;
    // 3.2 s 内按 1 s 退避，加上首轮，最多 5 次左右；绝不是 50 Hz 的 160 次
    expect(attemptsToDead).toBeLessThanOrEqual(6);
    expect(transport.written).toContain('ADDR 1');
    scheduler.dispose();
  });

  it('桥接回 ERR TIMEOUT 时按"该地址无设备"计失败', async () => {
    const { clock, transport, scheduler, sessions } = await setup();
    transport.auto = true;
    // 让假总线对所有命令回 ERR TIMEOUT（模拟桥接后面没有板子）
    transport.autoReplyOverride = () => 'ERR TIMEOUT';

    const pending = scheduler.submit(0, { kind: 'readFeedback' }, 'user');
    await clock.advance(10);
    const result = await pending;
    expect(result).toMatchObject({ ok: false, error: { kind: 'device', code: 'TIMEOUT' } });
    expect(sessions[0]?.snapshot().failures).toBe(1);
    expect(sessions[0]?.snapshot().link).not.toBe('online');
    scheduler.dispose();
  });

  it('轮询里 SYSREG 每 5 拍、VBUS 每 20 拍（与 web/index.html 一致）', async () => {
    const { clock, transport, scheduler } = await setup();
    transport.auto = true;
    scheduler.setPolling(true);
    await clock.advance(20 * 30);
    scheduler.setPolling(false);
    await flush();

    const fbIndexes = transport.written
      .map((line, index) => ({ line, index }))
      .filter((entry) => entry.line !== 'ADDR 0');
    const sysregIndex = fbIndexes.findIndex((entry) => entry.line === 'SYSREG');
    const vbusIndex = fbIndexes.findIndex((entry) => entry.line === 'VBUS');

    expect(sysregIndex).toBe(4);
    expect(vbusIndex).toBeGreaterThan(sysregIndex);
    expect(fbIndexes.filter((e) => e.line === 'SYSREG').length).toBeGreaterThan(1);
    scheduler.dispose();
  });
});
