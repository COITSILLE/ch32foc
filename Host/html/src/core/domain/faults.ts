/**
 * SYSREG 位表 → 人话。
 *
 * 位定义来自桥接固件 `User/device/motor2204.h`：
 *  - warning: bit[7:0]，已知 0x10/0x20/0x40/0x80
 *  - fault  : bit[15:8]，已知 0x20/0x40/0x80（过流/过压/欠压）
 *  - state  : bit[23:16]，SysState 枚举 0..3
 */
export const STATE_LABELS: readonly string[] = ['初始化', '运行', '故障', '校准中'];

export interface FlagDefinition {
  bit: number;
  label: string;
}

export const WARN_FLAGS: readonly FlagDefinition[] = [
  { bit: 0x10, label: 'flash 读失败' },
  { bit: 0x20, label: '编码器初始化失败' },
  { bit: 0x40, label: '母线跌落' },
  { bit: 0x80, label: '母线回充' },
];

export const FAULT_FLAGS: readonly FlagDefinition[] = [
  { bit: 0x20, label: '过流' },
  { bit: 0x40, label: '过压' },
  { bit: 0x80, label: '欠压' },
];

/**
 * `SYSREG` bit[31:24]：最近一次复位的原因（FOC `main.cpp: sys_reg.mcu_flags`）。
 * 只反映上电那一刻 RCC 的复位标志，不影响运行状态。
 */
export const RESET_FLAGS: readonly FlagDefinition[] = [
  { bit: 0x01, label: '上电复位' },
  { bit: 0x02, label: '软件复位' },
  { bit: 0x04, label: '看门狗复位' },
  { bit: 0x08, label: '低功耗唤醒复位' },
  { bit: 0x10, label: 'ADC 看门狗复位' },
];

export type Severity = 'ok' | 'warn' | 'fault';

export interface SysRegText {
  stateLabel: string;
  warnings: string[];
  faults: string[];
  severity: Severity;
}

export function stateLabel(state: number): string {
  return STATE_LABELS[state] ?? `未知(${state})`;
}

export function decodeSysReg(state: number, warn: number, fault: number): SysRegText {
  const warnings = WARN_FLAGS.filter((f) => (warn & f.bit) !== 0).map((f) => f.label);
  const faults = FAULT_FLAGS.filter((f) => (fault & f.bit) !== 0).map((f) => f.label);
  const severity: Severity = faults.length > 0 ? 'fault' : warnings.length > 0 ? 'warn' : 'ok';
  return { stateLabel: stateLabel(state), warnings, faults, severity };
}

/** 未知位保留十六进制，避免"界面说正常、实际有未定义故障位"的误判。 */
export function unknownBitsText(warn: number, fault: number): string | null {
  const knownWarn = WARN_FLAGS.reduce((acc, f) => acc | f.bit, 0);
  const knownFault = FAULT_FLAGS.reduce((acc, f) => acc | f.bit, 0);
  const extraWarn = warn & ~knownWarn & 0xff;
  const extraFault = fault & ~knownFault & 0xff;
  const parts: string[] = [];
  if (extraWarn !== 0) parts.push(`未定义告警位 0x${extraWarn.toString(16).toUpperCase()}`);
  if (extraFault !== 0) parts.push(`未定义故障位 0x${extraFault.toString(16).toUpperCase()}`);
  return parts.length > 0 ? parts.join('，') : null;
}

export function isFaultState(state: number): boolean {
  return state === 2;
}

/** 从 `SYSREG.raw` 解出复位原因（bit[31:24]）。 */
export function resetReasons(raw: number): string[] {
  const flags = (raw >>> 24) & 0xff;
  return RESET_FLAGS.filter((f) => (flags & f.bit) !== 0).map((f) => f.label);
}
