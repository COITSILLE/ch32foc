import { describe, expect, it } from 'vitest';
import { encode } from '../../src/core/protocol/commands';

describe('protocol.commands — 命令编码', () => {
  it('ZETA 只带 SPD / AB，值按定点十进制输出', () => {
    expect(encode({ kind: 'writeZeta', which: 'SPD', value: 1 })).toBe('ZETA SPD 1');
    expect(encode({ kind: 'writeZeta', which: 'AB', value: 0.85 })).toBe('ZETA AB 0.85');
  });

  it('ZOFF 不带参数', () => {
    expect(encode({ kind: 'zeroOffset' })).toBe('ZOFF');
  });

  it('浮点参数不用科学计数法、最多 4 位小数', () => {
    expect(encode({ kind: 'writeBandwidth', which: 'CUR', hz: 4000 })).toBe('BW CUR 4000');
    expect(encode({ kind: 'writeZeta', which: 'SPD', value: 0.000012 })).toBe('ZETA SPD 0');
    expect(encode({ kind: 'writeOverCurrent', amps: 2.5, ms: 3000 })).toBe('OVC 2.5 3000');
  });
});
