import type { ErrCode } from './protocol/types';

/**
 * 跨层错误模型（架构文档 §7）。
 *
 * 放在 `core` 是因为 `core` 自己也要产出错误；`app/errors.ts` 只负责加上用户文案。
 */
export type AppError =
  | {
      kind: 'transport';
      code: 'unsupported' | 'open-failed' | 'write-failed' | 'closed';
      detail?: string;
    }
  | { kind: 'timeout'; addr: number; detail?: string }
  | { kind: 'device'; code: ErrCode; addr: number; raw: string }
  | { kind: 'protocol'; raw: string }
  | {
      kind: 'guard';
      reason: 'debug-locked' | 'not-enabled' | 'range' | 'not-connected';
      detail?: string;
    };

export function transportError(
  code: 'unsupported' | 'open-failed' | 'write-failed' | 'closed',
  detail?: string,
): AppError {
  return detail === undefined ? { kind: 'transport', code } : { kind: 'transport', code, detail };
}

export function timeoutError(addr: number, detail?: string): AppError {
  return detail === undefined ? { kind: 'timeout', addr } : { kind: 'timeout', addr, detail };
}

export function deviceError(code: ErrCode, addr: number, raw: string): AppError {
  return { kind: 'device', code, addr, raw };
}

export function protocolError(raw: string): AppError {
  return { kind: 'protocol', raw };
}

export function guardError(
  reason: 'debug-locked' | 'not-enabled' | 'range' | 'not-connected',
  detail?: string,
): AppError {
  return detail === undefined ? { kind: 'guard', reason } : { kind: 'guard', reason, detail };
}

/** Transport 层的细化错误码 → 跨层错误模型（读失败统一按链路关闭处理）。 */
export function fromTransportError(error: {
  code: 'unsupported' | 'open-failed' | 'read-failed' | 'write-failed' | 'closed';
  message: string;
}): AppError {
  const code = error.code === 'read-failed' ? 'closed' : error.code;
  return transportError(code, error.message);
}
