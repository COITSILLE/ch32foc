import { MAX_LINE } from './limits';

export interface FrameResult {
  /** 这一块数据里拼出的完整行（已去掉行尾的 `\r`）。 */
  lines: string[];
  /** 这一块数据里因为超长被丢弃的行数。 */
  overlong: number;
}

/**
 * 字节流 → 完整行。
 *
 * 下位机回包以 `\r\n` 结束，但 `\n` 单独出现（换行符被拆包、或别的工具写入）也要能处理；
 * 超过 `MAX_LINE` 的行整行丢弃——下位机对超长命令也是这么做的，保持两边一致。
 */
export class LineFramer {
  private buffer = '';
  private discarding = false;
  private readonly maxLen: number;

  constructor(maxLen: number = MAX_LINE) {
    this.maxLen = maxLen;
  }

  push(chunk: string): FrameResult {
    const lines: string[] = [];
    let overlong = 0;

    this.buffer += chunk;

    for (;;) {
      const idx = this.buffer.indexOf('\n');
      if (idx < 0) break;

      const rawLine = this.buffer.slice(0, idx);
      this.buffer = this.buffer.slice(idx + 1);

      if (this.discarding) {
        // 超长行剩下的尾巴：整行已经计过一次，这里直接丢掉
        this.discarding = false;
        continue;
      }

      const line = rawLine.endsWith('\r') ? rawLine.slice(0, -1) : rawLine;
      if (line.length > this.maxLen) {
        overlong += 1;
        continue;
      }
      if (line.length > 0) lines.push(line);
    }

    if (this.buffer.length > this.maxLen) {
      this.buffer = '';
      this.discarding = true;
      overlong += 1;
    }

    return { lines, overlong };
  }

  /** 链路断开时清理半包。 */
  reset(): void {
    this.buffer = '';
    this.discarding = false;
  }

  /** 已缓存但还没成行的数据（调试用）。 */
  get pending(): string {
    return this.buffer;
  }
}
