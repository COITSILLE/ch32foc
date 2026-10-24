/** 协议层的判别联合：命令、应答、错误码。UI 只 switch kind，不再自己拆字符串。 */

export type LoopMode = 'cur' | 'spd' | 'pos';

/** 闭环模式对应的固件枚举值（`MODE` 命令也接受 0..2）。 */
export const LOOP_MODE_VALUE: Readonly<Record<LoopMode, number>> = { cur: 0, spd: 1, pos: 2 };

/** `MODE` 命令参数（大写；固件内部会再转一次大写）。 */
export const LOOP_MODE_ARG: Readonly<Record<LoopMode, string>> = { cur: 'CUR', spd: 'SPD', pos: 'POS' };

export const LOOP_MODE_TARGET_CMD: Readonly<Record<LoopMode, string>> = { cur: 'IQ', spd: 'SPD', pos: 'POS' };

export type BandwidthChannel = 'CUR' | 'SPD' | 'POS' | 'ESO' | 'AB';

/** 有阻尼比可调的两条环路（固件 `SET_SPEED_ZETA` / `SET_ABFILT_ZETA`）。 */
export type DampingChannel = 'SPD' | 'AB';

/** 固件 `ERR *` 的码表（含桥接层自己产生的 `ERR LINE` / `ERR DADDR`）。 */
export type ErrCode =
  | 'ARG'
  | 'CMD'
  | 'DEBUG'
  | 'TIMEOUT'
  | 'FAULT'
  | 'UNSUPPORTED'
  | 'FRAME'
  | 'VBUS'
  | 'LINE'
  | 'DADDR';

const ERR_CODES: readonly ErrCode[] = [
  'ARG',
  'CMD',
  'DEBUG',
  'TIMEOUT',
  'FAULT',
  'UNSUPPORTED',
  'FRAME',
  'VBUS',
  'LINE',
  'DADDR',
];

export function isErrCode(value: string): value is ErrCode {
  return (ERR_CODES as readonly string[]).includes(value);
}

export type Command =
  | { kind: 'ping' }
  | { kind: 'setAddr'; addr: number }
  | { kind: 'setMode'; mode: LoopMode }
  | { kind: 'enable' }
  | { kind: 'disable' }
  | { kind: 'setTarget'; mode: LoopMode; value: number }
  | { kind: 'readFeedback' }
  | { kind: 'readVbus' }
  | { kind: 'readSysreg' }
  | { kind: 'clearFault' }
  | { kind: 'info' }
  | { kind: 'calibrate' }
  | { kind: 'readDeviceAddr' }
  | { kind: 'writeBaud'; baud: number }
  | { kind: 'writeBandwidth'; which: BandwidthChannel; hz: number }
  | { kind: 'writeZeta'; which: DampingChannel; value: number }
  | { kind: 'writeOverCurrent'; amps: number; ms: number }
  | { kind: 'zeroOffset' }
  | { kind: 'defaults' }
  | { kind: 'save' };

export type Reply =
  | { kind: 'pong'; raw: string }
  | { kind: 'feedback'; iqA: number; speedRadS: number; posRad: number; raw: string }
  | { kind: 'vbus'; volts: number; raw: string }
  | { kind: 'sysreg'; value: number; state: number; warn: number; fault: number; raw: string }
  | { kind: 'ack'; cmd: string; args: readonly string[]; raw: string }
  | { kind: 'info'; firmwareVersion: number; chipId: number; deviceId: number; raw: string }
  | { kind: 'deviceAddr'; addr: number; raw: string }
  | { kind: 'calibStarted'; raw: string }
  | { kind: 'error'; code: ErrCode; raw: string }
  | { kind: 'unknown'; raw: string };

export function replyKindLabel(reply: Reply): string {
  switch (reply.kind) {
    case 'ack':
      return `OK ${reply.cmd}`;
    case 'calibStarted':
      return 'START CALIB';
    case 'error':
      return `ERR ${reply.code}`;
    case 'sysreg':
    case 'feedback':
    case 'vbus':
    case 'info':
    case 'deviceAddr':
    case 'pong':
    case 'unknown':
      return reply.raw;
  }
}
