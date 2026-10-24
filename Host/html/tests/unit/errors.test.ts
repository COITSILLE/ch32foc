import { describe, expect, it } from 'vitest';
import { errorText, errorTone } from '../../src/app/errors';
import { deviceError, timeoutError, transportError } from '../../src/core/app-error';

const fault = deviceError('FAULT', 0, 'ERR FAULT');

describe('app.errors — 错误文案带命令上下文', () => {
  it('清故障被拒 ≠ 电机在故障态', () => {
    const text = errorText(fault, { command: 'clearFault', stateLabel: '运行' });
    expect(text).toContain('当前不在故障态');
    expect(text).not.toContain('请先「清故障」');
    expect(text).toContain('运行');
  });

  it('使能被拒才是"电机在故障态"', () => {
    const text = errorText(fault, { command: 'enable', stateLabel: '故障' });
    expect(text).toContain('电机处于故障态');
    expect(text).toContain('请先「清故障」');
  });

  it('没有上下文时不给出误导性结论', () => {
    const text = errorText(fault);
    expect(text).toContain('下位机拒绝了该命令');
  });

  it('常见错误码各自有独立文案', () => {
    expect(errorText(deviceError('DEBUG', 1, 'ERR DEBUG'))).toContain('DEBUG');
    expect(errorText(deviceError('VBUS', 1, 'ERR VBUS'))).toContain('母线');
    expect(errorText(deviceError('TIMEOUT', 1, 'ERR TIMEOUT'))).toContain('ERR TIMEOUT');
    expect(errorText(timeoutError(2))).toContain('设备 2');
    expect(errorText(transportError('closed'))).toContain('串口已关闭');
  });

  it('DEBUG 与 FAULT 用警告色，其余用错误色', () => {
    expect(errorTone(deviceError('FAULT', 0, 'ERR FAULT'))).toBe('warn');
    expect(errorTone(deviceError('DEBUG', 0, 'ERR DEBUG'))).toBe('warn');
    expect(errorTone(deviceError('ARG', 0, 'ERR ARG'))).toBe('error');
    expect(errorTone(timeoutError(0))).toBe('error');
  });
});
