/**
 * 单位换算的唯一出口。
 *
 * 已经与固件核对过（`learn/foc/User/app/foc_driver.h` + `main.cpp` 的 `fill_feedback`）：
 *  - `FB` 的速度是**机械角速度**，单位 rad/s（内部 q24 乘 `SPD_UNIT` 还原）
 *  - `FB` 的位置是累计机械角，单位 rad
 *  - 电流 A、母线电压 V、带宽 Hz，都已经是物理量
 * 所以这里只做"显示单位 ↔ 协议单位"的换算，绝不引入极对数之类的隐含系数。
 */

export const RAD_PER_SEC_PER_RPM = Math.PI / 30;
export const TAU = Math.PI * 2;

export function rpmToRadPerSec(rpm: number): number {
  return rpm * RAD_PER_SEC_PER_RPM;
}

export function radPerSecToRpm(radPerSec: number): number {
  return radPerSec / RAD_PER_SEC_PER_RPM;
}

export function degToRad(deg: number): number {
  return (deg * Math.PI) / 180;
}

export function radToDeg(rad: number): number {
  return (rad * 180) / Math.PI;
}

export function ampToMilliAmp(amp: number): number {
  return amp * 1000;
}

export function milliAmpToAmp(mA: number): number {
  return mA / 1000;
}

/** 归一化到 [0, 2π)。 */
export function wrapAngle(rad: number): number {
  const r = rad % TAU;
  return r < 0 ? r + TAU : r;
}

/** 归一化到 (-π, π]。 */
export function wrapSignedAngle(rad: number): number {
  const r = wrapAngle(rad);
  return r > Math.PI ? r - TAU : r;
}

/** 界面展示用的单位标签；改单位只改这里。速度统一 rad/s（与协议一致，不做显示换算）。 */
export const UNIT_LABEL = {
  iqA: 'A',
  speedRadS: 'rad/s',
  posRad: 'rad',
  vbusV: 'V',
  bandwidthHz: 'Hz',
  timeMs: 'ms',
  damping: 'ζ',
} as const;

/**
 * 界面速度档位（rad/s）。按 2204 的实际量级给，并取"能用又不会吓人"的小幅值：
 * 15 rad/s ≈ 143 rpm，30 rad/s ≈ 286 rpm。
 */
export const SPEED_PRESETS = {
  stepSmall: 15,
  stepLarge: 30,
} as const;

/**
 * 幅值默认值按**当前闭环模式**给（界面上可改）：
 *  - spd: 30 rad/s（≈ 286 rpm）
 *  - pos: 0.5 rad（≈ 28.6°），以开始时的位置为中心摆动
 *  - cur: 0.5 A
 */
export const AMPLITUDE_PRESETS = {
  spd: 30,
  pos: 0.5,
  cur: 0.5,
} as const;

/** 幅值输入框的步进，按模式给不同精度。 */
export const AMPLITUDE_STEP = {
  spd: 5,
  pos: 0.1,
  cur: 0.1,
} as const;
