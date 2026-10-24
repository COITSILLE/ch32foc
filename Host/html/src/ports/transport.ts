import type { Unsubscribe } from '../shared/subscribe';

export type TransportKind = 'web-serial' | 'replay';

export type TransportState = 'closed' | 'open' | 'error';

export interface TransportError {
  code: 'unsupported' | 'open-failed' | 'read-failed' | 'write-failed' | 'closed';
  message: string;
}

export interface OpenOptions {
  baudRate: number;
  deviceLabel?: string;
}

/**
 * 唯一与"字节"打交道的抽象。
 *
 * 不做协议解析、不做重试、不关心地址——这些都在 `core` 里，
 * 换实现（真机串口 / 录制回放）时上层代码完全不用改。
 */
export interface Transport {
  readonly kind: TransportKind;
  readonly state: TransportState;
  /** 给人看的名字（串口路径 / "回放"）。 */
  readonly label: string;
  open(opts: OpenOptions): Promise<void>;
  close(): Promise<void>;
  /** 只写整行；是否补换行由实现负责。 */
  write(line: string): Promise<void>;
  onLine(cb: (line: string) => void): Unsubscribe;
  onError(cb: (e: TransportError) => void): Unsubscribe;
}
