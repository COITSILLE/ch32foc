/**
 * Web Serial 的最小类型补充。
 *
 * 只声明本工程用到的成员：完整实现会把类型锁死在某个 TS DOM 版本上，
 * 而 Chrome 的串口 API 一直没进标准库。
 */

interface SerialPortOpenOptions {
  baudRate: number;
  dataBits?: 7 | 8;
  stopBits?: 1 | 2;
  parity?: 'none' | 'even' | 'odd';
  bufferSize?: number;
  flowControl?: 'none' | 'hardware';
}

interface SerialPort extends EventTarget {
  readonly readable: ReadableStream<Uint8Array> | null;
  readonly writable: WritableStream<Uint8Array> | null;
  open(options: SerialPortOpenOptions): Promise<void>;
  close(): Promise<void>;
  forget?(): Promise<void>;
  getInfo?(): { usbVendorId?: number; usbProductId?: number };
}

interface SerialPortFilter {
  usbVendorId?: number;
  usbProductId?: number;
}

interface SerialPortRequestOptions {
  filters?: SerialPortFilter[];
}

interface Serial {
  requestPort(options?: SerialPortRequestOptions): Promise<SerialPort>;
  getPorts(): Promise<SerialPort[]>;
  addEventListener(
    type: 'disconnect',
    listener: (event: Event & { target: SerialPort }) => void,
  ): void;
  removeEventListener(
    type: 'disconnect',
    listener: (event: Event & { target: SerialPort }) => void,
  ): void;
}

interface Navigator {
  readonly serial?: Serial;
}
