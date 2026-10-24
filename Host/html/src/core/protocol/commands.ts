import { assert } from '../../shared/assert';
import { fmtProtocolFloat } from '../../shared/fmt';
import { LOOP_MODE_ARG, LOOP_MODE_TARGET_CMD, type Command } from './types';
import { MAX_LINE } from './limits';

/**
 * 命令 → ASCII 行。
 *
 * 只负责拼字符串和长度校验，不做单位换算（那属于 `core/domain/units.ts`），
 * 也不做地址切换（总线层在每条命令前用 `ADDR n` 抢占桥接地址）。
 */
export function encode(cmd: Command): string {
  const line = encodeUnchecked(cmd);
  assert(
    line.length <= MAX_LINE,
    `command exceeds ${MAX_LINE} chars (${line.length}): ${line}`,
  );
  return line;
}

function encodeUnchecked(cmd: Command): string {
  switch (cmd.kind) {
    case 'ping':
      return 'PING';
    case 'setAddr':
      return `ADDR ${Math.trunc(cmd.addr)}`;
    case 'setMode':
      return `MODE ${LOOP_MODE_ARG[cmd.mode]}`;
    case 'enable':
      return 'ENABLE';
    case 'disable':
      return 'DISABLE';
    case 'setTarget':
      return `${LOOP_MODE_TARGET_CMD[cmd.mode]} ${fmtProtocolFloat(cmd.value)}`;
    case 'readFeedback':
      return 'FB';
    case 'readVbus':
      return 'VBUS';
    case 'readSysreg':
      return 'SYSREG';
    case 'clearFault':
      return 'CLEAR';
    case 'info':
      return 'INFO';
    case 'calibrate':
      return 'CALIB';
    case 'readDeviceAddr':
      return 'DADDR';
    case 'writeBaud':
      return `BAUD ${Math.trunc(cmd.baud)}`;
    case 'writeBandwidth':
      return `BW ${cmd.which} ${fmtProtocolFloat(cmd.hz)}`;
    case 'writeZeta':
      return `ZETA ${cmd.which} ${fmtProtocolFloat(cmd.value)}`;
    case 'writeOverCurrent':
      return `OVC ${fmtProtocolFloat(cmd.amps)} ${Math.trunc(cmd.ms)}`;
    case 'zeroOffset':
      return 'ZOFF';
    case 'defaults':
      return 'DEFAULTS';
    case 'save':
      return 'SAVE';
  }
}
