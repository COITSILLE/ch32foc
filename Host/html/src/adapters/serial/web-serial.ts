import type {
  OpenOptions,
  Transport,
  TransportError,
  TransportKind,
  TransportState,
} from '../../ports/transport';
import { LineFramer } from '../../core/protocol/framing';
import { Emitter, type Unsubscribe } from '../../shared/subscribe';

/**
 * `navigator.serial` 实现。
 *
 * 只做字节收发与行切分：不发 PING、不重试、不解析协议——
 * 这些行为在 `core` 里，仿真与真机才能完全一致。
 */
export class WebSerialTransport implements Transport {
  readonly kind: TransportKind = 'web-serial';

  private port: SerialPort | null = null;
  private writer: WritableStreamDefaultWriter<Uint8Array> | null = null;
  private reader: ReadableStreamDefaultReader<Uint8Array> | null = null;
  private readonly decoder = new TextDecoder();
  private readonly framer = new LineFramer();
  private readonly encoder = new TextEncoder();
  private readonly lines = new Emitter<string>();
  private readonly errors = new Emitter<TransportError>();

  private stateValue: TransportState = 'closed';
  private labelValue = '未连接';
  private closing = false;

  constructor(private readonly serialFactory?: () => Serial | undefined) {}

  get state(): TransportState {
    return this.stateValue;
  }

  get label(): string {
    return this.labelValue;
  }

  get isSupported(): boolean {
    return this.serial() !== undefined;
  }

  async open(opts: OpenOptions): Promise<void> {
    const serial = this.serial();
    if (serial === undefined) {
      throw new Error('此浏览器不支持 Web Serial，请使用 Chrome / Edge');
    }

    try {
      const port = await serial.requestPort();
      await port.open({ baudRate: opts.baudRate });
      this.port = port;
      this.labelValue = opts.deviceLabel ?? describePort(port);
      this.writer = port.writable?.getWriter() ?? null;
      this.stateValue = 'open';
      this.closing = false;
      serial.addEventListener('disconnect', this.handleDisconnect);
      void this.readLoop();
    } catch (e) {
      this.stateValue = 'error';
      throw e instanceof Error ? e : new Error(String(e));
    }
  }

  async close(): Promise<void> {
    this.closing = true;
    this.stateValue = 'closed';

    try {
      await this.reader?.cancel();
    } catch {
      // 断开时 cancel 会抛错，忽略
    }
    this.reader = null;

    try {
      this.writer?.releaseLock();
    } catch {
      // 同上
    }
    this.writer = null;

    const port = this.port;
    if (port !== null) {
      try {
        await port.close();
      } catch {
        // 端口可能已被拔出
      }
      this.serial()?.removeEventListener('disconnect', this.handleDisconnect);
    }
    this.port = null;
    this.framer.reset();
  }

  async write(line: string): Promise<void> {
    if (this.writer === null || this.stateValue !== 'open') {
      throw new Error('串口未打开');
    }
    await this.writer.write(this.encoder.encode(`${line}\n`));
  }

  onLine(cb: (line: string) => void): Unsubscribe {
    return this.lines.subscribe(cb);
  }

  onError(cb: (e: TransportError) => void): Unsubscribe {
    return this.errors.subscribe(cb);
  }

  private serial(): Serial | undefined {
    if (this.serialFactory !== undefined) return this.serialFactory();
    return navigator.serial;
  }

  private readonly handleDisconnect = (event: Event): void => {
    if (event.target !== this.port) return;
    this.stateValue = 'error';
    this.errors.emit({ code: 'closed', message: '串口已拔出' });
    void this.close();
  };

  private async readLoop(): Promise<void> {
    const port = this.port;
    if (port === null || port.readable === null) {
      this.errors.emit({ code: 'read-failed', message: '串口不可读' });
      return;
    }

    const reader = port.readable.getReader();
    this.reader = reader;
    try {
      for (;;) {
        const { value, done } = await reader.read();
        if (done) break;
        if (value === undefined) continue;
        const { lines } = this.framer.push(this.decoder.decode(value, { stream: true }));
        for (const line of lines) this.lines.emit(line);
      }
    } catch (e) {
      if (!this.closing) {
        this.stateValue = 'error';
        this.errors.emit({
          code: 'read-failed',
          message: e instanceof Error ? e.message : String(e),
        });
      }
    } finally {
      try {
        reader.releaseLock();
      } catch {
        // 已释放
      }
      this.reader = null;
    }
  }
}

function describePort(port: SerialPort): string {
  const info = port.getInfo?.();
  if (info?.usbVendorId === undefined) return 'USB 串口';
  const vid = info.usbVendorId.toString(16).padStart(4, '0');
  const pid = (info.usbProductId ?? 0).toString(16).padStart(4, '0');
  return `USB ${vid}:${pid}`;
}
