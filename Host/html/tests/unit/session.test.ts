import { describe, expect, it } from 'vitest';
import { DeviceSession } from '../../src/core/domain/session';
import { FakeClock, ScriptedBus } from '../fixtures/session-scenarios';

describe('domain.session — 单台设备状态机', () => {
  it('FB 回包更新电流 / 速度 / 位置，并置在线', () => {
    const clock = new FakeClock();
    const session = new DeviceSession(0, new ScriptedBus(), clock);
    session.applyReply({ kind: 'feedback', iqA: 0.5, speedRadS: -12, posRad: 3.14, raw: 'FB 0.500 -12.000 3.1400' });
    expect(session.snapshot()).toMatchObject({ link: 'online', iqA: 0.5, speedRadS: -12, posRad: 3.14 });
  });

  it('SYSREG 故障态（state=2）自动把使能标记清掉', () => {
    const clock = new FakeClock();
    const session = new DeviceSession(0, new ScriptedBus(), clock);
    session.applyReply({ kind: 'ack', cmd: 'ENABLE', args: [], raw: 'OK ENABLE' });
    expect(session.snapshot().enabled).toBe(true);
    session.applyReply({ kind: 'sysreg', value: 0x00020020, state: 2, warn: 0, fault: 0x20, raw: 'SYSREG ...' });
    expect(session.snapshot().enabled).toBe(false);
    expect(session.snapshot().sysreg.fault).toBe(0x20);
  });

  it('INFO 成功后解锁 DEBUG，ERR DEBUG 重新上锁', async () => {
    const clock = new FakeClock();
    const bus = new ScriptedBus();
    const session = new DeviceSession(0, bus, clock);

    expect(await session.exec({ kind: 'save' })).toMatchObject({
      ok: false,
      error: { kind: 'guard', reason: 'debug-locked' },
    });
    expect(bus.calls).toHaveLength(0);

    bus.reply({ kind: 'info', firmwareVersion: 1, chipId: 2, deviceId: 3, raw: 'INFO ...' });
    const info = await session.exec({ kind: 'info' });
    expect(info.ok).toBe(true);
    session.applyReply({ kind: 'info', firmwareVersion: 1, chipId: 2, deviceId: 3, raw: 'INFO ...' });
    expect(session.isDebugUnlocked).toBe(true);
    expect(session.snapshot().config).toMatchObject({ firmwareVersion: 1, chipId: 2, deviceId: 3 });

    session.applyReply({ kind: 'error', code: 'DEBUG', raw: 'ERR DEBUG' });
    expect(session.isDebugUnlocked).toBe(false);
  });

  it('BW / OVC / BAUD 的 OK 回包写进 config', () => {
    const clock = new FakeClock();
    const session = new DeviceSession(0, new ScriptedBus(), clock);
    session.applyReply({ kind: 'ack', cmd: 'BW', args: ['CUR', '1500.000'], raw: 'OK BW CUR 1500.000' });
    session.applyReply({ kind: 'ack', cmd: 'OVC', args: ['2.000', '3000'], raw: 'OK OVC 2.000 3000' });
    session.applyReply({ kind: 'ack', cmd: 'BAUD', args: ['230400'], raw: 'OK BAUD 230400' });
    expect(session.snapshot().config).toMatchObject({
      bandwidthHz: { CUR: 1500 },
      overCurrentA: 2,
      overCurrentMs: 3000,
      baud: 230400,
    });
  });

  it('目标值回包同步 mode 与 target', () => {
    const clock = new FakeClock();
    const session = new DeviceSession(0, new ScriptedBus(), clock);
    session.applyReply({ kind: 'ack', cmd: 'SPD', args: ['-1200.000'], raw: 'OK SPD -1200.000' });
    expect(session.snapshot()).toMatchObject({ mode: 'spd', target: -1200 });
  });

  it('连续 3 次超时置 stale，一次成功立刻恢复', () => {
    const clock = new FakeClock();
    const session = new DeviceSession(0, new ScriptedBus(), clock);
    session.noteSuccess();
    session.noteFailure();
    session.noteFailure();
    expect(session.snapshot().link).toBe('online');
    session.noteFailure();
    expect(session.snapshot()).toMatchObject({ link: 'stale', failures: 3 });
    session.noteSuccess();
    expect(session.snapshot()).toMatchObject({ link: 'online', failures: 0 });
  });

  it('快照变化会通知订阅者，且每次都是新对象', () => {
    const clock = new FakeClock();
    const session = new DeviceSession(0, new ScriptedBus(), clock);
    const seen: number[] = [];
    const unsub = session.subscribe((snapshot) => seen.push(snapshot.speedRadS));
    session.applyReply({ kind: 'feedback', iqA: 0, speedRadS: 10, posRad: 0, raw: 'FB 0 10 0' });
    session.applyReply({ kind: 'feedback', iqA: 0, speedRadS: 20, posRad: 0, raw: 'FB 0 20 0' });
    unsub();
    session.applyReply({ kind: 'feedback', iqA: 0, speedRadS: 30, posRad: 0, raw: 'FB 0 30 0' });
    expect(seen).toEqual([10, 20]);
  });

  it('任何回包都会刷新 lastSeenMs', async () => {
    const clock = new FakeClock();
    const session = new DeviceSession(0, new ScriptedBus(), clock);
    await clock.advance(500);
    session.applyReply({ kind: 'pong', raw: 'PONG' });
    expect(session.snapshot().lastSeenMs).toBe(500);
  });
});
