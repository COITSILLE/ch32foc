import { describe, expect, it } from 'vitest';
import { decodeSysReg, resetReasons, unknownBitsText } from '../../src/core/domain/faults';

describe('SYSREG 解码', () => {
  it('warning / fault / state 各占一个字节', () => {
    const text = decodeSysReg(2, 0x10, 0x20);
    expect(text.stateLabel).toBe('故障');
    expect(text.warnings).toEqual(['flash 读失败']);
    expect(text.faults).toEqual(['过流']);
    expect(text.severity).toBe('fault');
  });

  it('bit[31:24] 是复位原因', () => {
    expect(resetReasons(0x05000010)).toEqual(['上电复位', '看门狗复位']);
    expect(resetReasons(0x00000000)).toEqual([]);
  });

  it('未定义位保留十六进制提示', () => {
    expect(unknownBitsText(0x01, 0x01)).toContain('未定义告警位 0x1');
    expect(unknownBitsText(0x01, 0x01)).toContain('未定义故障位 0x1');
    expect(unknownBitsText(0x90, 0xe0)).toBeNull();
  });
});
