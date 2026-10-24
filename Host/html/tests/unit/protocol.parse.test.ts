import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { describe, expect, it } from 'vitest';
import { parse } from '../../src/core/protocol/parse';

const fixturePath = fileURLToPath(new URL('../fixtures/replies.txt', import.meta.url));

function loadFixture(): { kind: string; raw: string }[] {
  const text = readFileSync(fixturePath, 'utf8');
  return text
    .split(/\r?\n/)
    .map((line) => line.trim())
    .filter((line) => line.length > 0 && !line.startsWith('#'))
    .map((line) => {
      const [kind = '', raw = ''] = line.split('\t');
      return { kind, raw };
    });
}

describe('protocol.parse — 固件真实回包', () => {
  const cases = loadFixture();

  it('fixture 覆盖了所有回包类型', () => {
    const kinds = new Set(cases.map((c) => c.kind));
    for (const expected of ['pong', 'feedback', 'vbus', 'sysreg', 'info', 'deviceAddr', 'calibStarted', 'ack', 'error', 'unknown']) {
      expect(kinds.has(expected), `fixture 缺少 ${expected} 样本`).toBe(true);
    }
  });

  for (const { kind, raw } of cases) {
    it(`${kind} ← ${raw}`, () => {
      expect(parse(raw).kind).toBe(kind);
    });
  }

  it('FB 字段顺序是 iq / speed / pos', () => {
    const reply = parse('FB 0.123 -12.345 6.2832');
    expect(reply).toMatchObject({ kind: 'feedback', iqA: 0.123, speedRadS: -12.345, posRad: 6.2832 });
  });

  it('SYSREG 按 raw / state / warn / fault 解析十六进制', () => {
    const reply = parse('SYSREG 0x00020010 2 0x00000010 0x00000020');
    expect(reply).toMatchObject({ kind: 'sysreg', value: 0x00020010, state: 2, warn: 0x10, fault: 0x20 });
  });

  it('INFO 三个字段都是十六进制版本/ID', () => {
    const reply = parse('INFO 0x00010000 0x00000413 0x00002204');
    expect(reply).toMatchObject({
      kind: 'info',
      firmwareVersion: 0x00010000,
      chipId: 0x00000413,
      deviceId: 0x00002204,
    });
  });

  it('OK 命令带上参数时保留 cmd 与 args', () => {
    expect(parse('OK BW CUR 1500.000')).toMatchObject({ kind: 'ack', cmd: 'BW', args: ['CUR', '1500.000'] });
    expect(parse('OK OVC 2.000 3000')).toMatchObject({ kind: 'ack', cmd: 'OVC', args: ['2.000', '3000'] });
    expect(parse('OK ENABLE')).toMatchObject({ kind: 'ack', cmd: 'ENABLE', args: [] });
  });

  it('ERR 未知码降级为 FRAME，但保留原始行', () => {
    expect(parse('ERR WEIRD')).toEqual({ kind: 'error', code: 'FRAME', raw: 'ERR WEIRD' });
  });

  it('ERR 已知码逐个保留（含桥接自己产生的 LINE / DADDR）', () => {
    const codes = ['ARG', 'CMD', 'DEBUG', 'TIMEOUT', 'FAULT', 'UNSUPPORTED', 'FRAME', 'VBUS', 'LINE', 'DADDR'];
    for (const code of codes) {
      expect(parse(`ERR ${code}`)).toMatchObject({ kind: 'error', code });
    }
  });

  it('OK ZETA / ZOFF 保留参数', () => {
    expect(parse('OK ZETA SPD 1.000')).toMatchObject({ kind: 'ack', cmd: 'ZETA', args: ['SPD', '1.000'] });
    expect(parse('OK ZOFF')).toMatchObject({ kind: 'ack', cmd: 'ZOFF', args: [] });
  });

  it('容忍多余空格与前后的制表符', () => {
    expect(parse('   FB   1.5\t 2.5   3.5  ').kind).toBe('feedback');
  });

  it('永不抛异常', () => {
    const weird = ['', ' ', '\t', 'FB', 'FB NaN NaN NaN', 'SYSREG 0x 1 2 3', '😀', 'OK', 'ERR'];
    for (const line of weird) {
      expect(() => parse(line)).not.toThrow();
    }
  });
});
