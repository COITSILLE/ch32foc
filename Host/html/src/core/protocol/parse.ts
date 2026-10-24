import { isErrCode, type Reply } from './types';

/**
 * 单行 → Reply。**永不抛异常**：认不出来就返回 `unknown`。
 *
 * 容错点（都来自真机跑出来的情况）：
 *  - 多余空格 / 制表符：统一按 `\s+` 切分
 *  - `0x` 前缀的十六进制：`SYSREG` / `INFO` 用
 *  - 字段缺失或数字非法：整行降级为 `unknown`，不要给半个快照
 *  - 小写回包（理论上不会出现）：按大写比较
 */
export function parse(line: string): Reply {
  const raw = line.trim();
  if (raw.length === 0) return { kind: 'unknown', raw };

  const tokens = raw.split(/\s+/);
  const head = (tokens[0] ?? '').toUpperCase();

  switch (head) {
    case 'PONG':
      return { kind: 'pong', raw };

    case 'FB': {
      const iq = toNumber(tokens[1]);
      const speed = toNumber(tokens[2]);
      const pos = toNumber(tokens[3]);
      if (iq === null || speed === null || pos === null) return { kind: 'unknown', raw };
      return { kind: 'feedback', iqA: iq, speedRadS: speed, posRad: pos, raw };
    }

    case 'VBUS': {
      const volts = toNumber(tokens[1]);
      if (volts === null) return { kind: 'unknown', raw };
      return { kind: 'vbus', volts, raw };
    }

    case 'SYSREG': {
      const value = toNumber(tokens[1]);
      const state = toNumber(tokens[2]);
      const warn = toNumber(tokens[3]);
      const fault = toNumber(tokens[4]);
      if (value === null || state === null || warn === null || fault === null) {
        return { kind: 'unknown', raw };
      }
      return { kind: 'sysreg', value, state, warn, fault, raw };
    }

    case 'INFO': {
      const fw = toNumber(tokens[1]);
      const chip = toNumber(tokens[2]);
      const device = toNumber(tokens[3]);
      if (fw === null || chip === null || device === null) return { kind: 'unknown', raw };
      return { kind: 'info', firmwareVersion: fw, chipId: chip, deviceId: device, raw };
    }

    case 'DADDR': {
      const addr = toInt(tokens[1]);
      if (addr === null) return { kind: 'unknown', raw };
      return { kind: 'deviceAddr', addr, raw };
    }

    case 'START':
      if ((tokens[1] ?? '').toUpperCase() === 'CALIB') return { kind: 'calibStarted', raw };
      return { kind: 'unknown', raw };

    case 'ERR': {
      const code = (tokens[1] ?? '').toUpperCase();
      // 未知错误码统一按 FRAME 处理，保住 raw 便于现场排查
      return { kind: 'error', code: isErrCode(code) ? code : 'FRAME', raw };
    }

    case 'OK': {
      const cmd = (tokens[1] ?? '').toUpperCase();
      return { kind: 'ack', cmd, args: tokens.slice(2), raw };
    }

    default:
      return { kind: 'unknown', raw };
  }
}

/** 十进制或 0x 十六进制；非法返回 null（不能返回 NaN，否则会污染快照）。 */
function toNumber(token: string | undefined): number | null {
  if (token === undefined || token.length === 0) return null;
  const text = token.trim();
  const hex = /^[+-]?0[xX][0-9a-fA-F]+$/.test(text);
  const dec = /^[+-]?(\d+\.?\d*|\.\d+)([eE][+-]?\d+)?$/.test(text);
  if (!hex && !dec) return null;
  const value = Number(text);
  return Number.isFinite(value) ? value : null;
}

function toInt(token: string | undefined): number | null {
  const value = toNumber(token);
  return value === null ? null : Math.trunc(value);
}
