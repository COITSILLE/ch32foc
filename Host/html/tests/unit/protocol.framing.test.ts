import { describe, expect, it } from 'vitest';
import { LineFramer } from '../../src/core/protocol/framing';

describe('protocol.framing — 字节流拆行', () => {
  it('半包：下一块才拼成完整行', () => {
    const framer = new LineFramer();
    expect(framer.push('FB 1.0 2.0')).toEqual({ lines: [], overlong: 0 });
    expect(framer.push(' 3.0\r\n').lines).toEqual(['FB 1.0 2.0 3.0']);
  });

  it('粘包：一次收到多行', () => {
    const framer = new LineFramer();
    const result = framer.push('PONG\r\nVBUS 24.0\r\nOK ENABLE\r\n');
    expect(result.lines).toEqual(['PONG', 'VBUS 24.0', 'OK ENABLE']);
  });

  it('CRLF 与裸 LF 混用', () => {
    const framer = new LineFramer();
    const result = framer.push('PONG\r\nVBUS 24.0\n');
    expect(result.lines).toEqual(['PONG', 'VBUS 24.0']);
  });

  it('空行不会产生空命令', () => {
    const framer = new LineFramer();
    expect(framer.push('\r\n\n\r\n').lines).toEqual([]);
  });

  it('超长行整行丢弃，后续行仍然正常', () => {
    const framer = new LineFramer(16);
    const long = 'FB ' + '9'.repeat(40);
    const result = framer.push(`${long}\r\nPONG\r\n`);
    expect(result.lines).toEqual(['PONG']);
    expect(result.overlong).toBe(1);
  });

  it('跨块超长行只计一次，且丢弃剩余字节', () => {
    const framer = new LineFramer(8);
    expect(framer.push('123456789').overlong).toBe(1);
    const tail = framer.push('0123456789\r\nPONG\r\n');
    expect(tail.overlong).toBe(0);
    expect(tail.lines).toEqual(['PONG']);
  });

  it('reset 丢弃半包', () => {
    const framer = new LineFramer();
    framer.push('FB 1.0');
    framer.reset();
    expect(framer.pending).toBe('');
    expect(framer.push(' 2.0 3.0\r\n').lines).toEqual([' 2.0 3.0']);
  });
});
