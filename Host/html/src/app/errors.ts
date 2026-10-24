import type { AppError } from '../core/app-error';
import type { Command } from '../core/protocol/types';

export type ErrorTone = 'error' | 'warn';

/** 同一条 `ERR FAULT` 在不同命令下含义完全不同，文案必须带上下文。 */
export interface ErrorContext {
  command?: Command['kind'];
  /** 最近一次读回的 state 文字（有就带上，便于判断"到底在不在故障态"）。 */
  stateLabel?: string;
}

/** 错误 → 用户文案：集中在这一处，以后要做 i18n 只改这里。 */
export function errorText(error: AppError, context: ErrorContext = {}): string {
  switch (error.kind) {
    case 'transport':
      switch (error.code) {
        case 'unsupported':
          return '此浏览器不支持 Web Serial，请使用 Chrome / Edge';
        case 'open-failed':
          return `打开串口失败${suffix(error.detail)}`;
        case 'write-failed':
          return `串口写入失败${suffix(error.detail)}`;
        case 'closed':
          return '串口已关闭，请重新连接';
      }
      break;
    case 'timeout':
      return `设备 ${error.addr} 300 ms 内无应答${suffix(error.detail)}`;
    case 'device':
      switch (error.code) {
        case 'ARG':
          return '参数非法（ERR ARG）';
        case 'CMD':
          return '桥接固件不支持该命令（ERR CMD）';
        case 'DEBUG':
          return '下位机不在 DEBUG 模式（上电时 PB0 需拉低）';
        case 'TIMEOUT':
          return '下位机与电机通信超时（ERR TIMEOUT）';
        case 'FAULT':
          return faultText(context);
        case 'UNSUPPORTED':
          return 'MCU 不支持该命令（ERR UNSUPPORTED）';
        case 'VBUS':
          return '母线电压过低，已拒绝校准（ERR VBUS）';
        case 'LINE':
          return '命令行过长，已被下位机丢弃（ERR LINE）';
        case 'DADDR':
          return '设备地址只读，不接受参数（ERR DADDR）';
        case 'FRAME':
          return '下位机返回帧错误（ERR FRAME）';
      }
      break;
    case 'protocol':
      return `无法解析的回包：${error.raw}`;
    case 'guard':
      switch (error.reason) {
        case 'debug-locked':
          return '请先「读取设备信息」解锁调试命令';
        case 'not-enabled':
          return '电机未使能';
        case 'range':
          return `超出允许范围${suffix(error.detail)}`;
        case 'not-connected':
          return '设备未连接';
      }
  }
}

/**
 * 固件里 `CLEAR_FAULT` 只在 `state == FAULT` 时执行，否则直接回 FAULT 帧；
 * `ENABLE` / `DISABLE` 反过来——只在 RUN 时执行。所以同样是 `ERR FAULT`：
 *  - 清故障时 = "你本来就不在故障态，没什么可清"
 *  - 使能时   = "板子真的在故障态，得先清"
 */
function faultText(context: ErrorContext): string {
  const state = context.stateLabel === undefined ? '' : `（当前状态：${context.stateLabel}）`;
  if (context.command === 'clearFault') {
    return `下位机拒绝了清故障：当前不在故障态，无需清除${state}（ERR FAULT）`;
  }
  if (context.command === 'enable' || context.command === 'disable') {
    return `电机处于故障态，请先「清故障」${state}（ERR FAULT）`;
  }
  return `下位机拒绝了该命令${state}（ERR FAULT）`;
}

export function errorTone(error: AppError): ErrorTone {
  return error.kind === 'device' && (error.code === 'FAULT' || error.code === 'DEBUG')
    ? 'warn'
    : 'error';
}

function suffix(detail: string | undefined): string {
  return detail === undefined || detail.length === 0 ? '' : `：${detail}`;
}
