import type {
  OpenOptions,
  Transport,
  TransportError,
  TransportKind,
  TransportState,
} from '../../ports/transport';
import { Emitter, type Unsubscribe } from '../../shared/subscribe';

export interface RecordedFrame {
  /** 相对录制开始的毫秒数。 */
  tMs: number;
  dir: 'tx' | 'rx';
  text: string;
}

/**
 * 回放 Transport：按录制时间轴重放 `rx` 行。
 *
 * 用途只有一个——复现现场问题：现场录一份收发日志，回来对着时间轴跑一遍，
 * 看是 `ERR CMD`（桥接没实现）还是 `ERR UNSUPPORTED`（MCU 不支持）。
 * 写出去的命令只记到 `sentLines`，不参与配对。
 */
export class ReplayTransport implements Transport {
  readonly kind: TransportKind = 'replay';

  private stateValue: TransportState = 'closed';
  private readonly lines = new Emitter<string>();
  private readonly errors = new Emitter<TransportError>();
  private readonly timers: number[] = [];
  private readonly sent: string[] = [];

  constructor(
    private readonly frames: readonly RecordedFrame[],
    private readonly speed = 1,
  ) {}

  get state(): TransportState {
    return this.stateValue;
  }

  get label(): string {
    return `回放（${this.frames.length} 帧）`;
  }

  /** 回放期间写出的命令，按顺序记录。 */
  get sentLines(): readonly string[] {
    return this.sent;
  }

  async open(_opts: OpenOptions): Promise<void> {
    this.stateValue = 'open';
    const first = this.frames.find((f) => f.dir === 'rx');
    const t0 = first?.tMs ?? 0;
    for (const frame of this.frames) {
      if (frame.dir !== 'rx') continue;
      const delay = Math.max(0, (frame.tMs - t0) / Math.max(0.01, this.speed));
      const id = globalThis.setTimeout(() => {
        this.lines.emit(frame.text);
      }, delay) as unknown as number;
      this.timers.push(id);
    }
  }

  async close(): Promise<void> {
    this.stateValue = 'closed';
    for (const id of this.timers.splice(0, this.timers.length)) {
      globalThis.clearTimeout(id);
    }
  }

  async write(line: string): Promise<void> {
    if (this.stateValue !== 'open') throw new Error('回放未开始');
    this.sent.push(line);
  }

  onLine(cb: (line: string) => void): Unsubscribe {
    return this.lines.subscribe(cb);
  }

  onError(cb: (e: TransportError) => void): Unsubscribe {
    return this.errors.subscribe(cb);
  }
}
