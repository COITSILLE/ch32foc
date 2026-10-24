/**
 * 协议常量。数值全部来自下位机源码，改之前先对照：
 *  - 桥接固件 `User/app/pc_link.h`（PC_LINE_CAPACITY / PC_RX_CAPACITY）
 *  - 桥接固件 `User/app/app_main.cpp`（各命令的参数解析与回包格式）
 *  - FOC 固件 `learn/foc/User/app/foc_driver.h`（速度 / 位置量程）
 */

/** 下位机 `PC_LINE_CAPACITY`：命令行超过这个长度会被整行丢弃并回 `ERR LINE`。 */
export const MAX_LINE = 96;

/** 下位机接收环形缓冲，决定"一条命令一个来回"的硬约束。 */
export const RX_CAPACITY = 128;

export const MIN_ADDR = 0;
export const MAX_ADDR = 7;

export const DEFAULT_BAUD = 115200;

/** 单条命令的应答超时：下位机偶尔忙/丢帧，短了会误判，长了链路会卡死。 */
export const REPLY_TIMEOUT_MS = 300;

/** `CALIB` / `SAVE` 固件内部阻塞约 1.1 s，期间本来就没有回包。 */
export const LONG_OPERATION_TIMEOUT_MS = 1500;

/** 轮询分频：与 `web/index.html` 已验证的行为一致（每 5 拍 SYSREG、每 20 拍 VBUS）。 */
export const SYSREG_DIV = 5;
export const VBUS_DIV = 20;

/** 总线带宽预算：115200 8N1 ≈ 11 520 B/s，一次往返约 31 B。 */
export const POLL_TOTAL_HZ = 220;
export const POLL_MIN_HZ = 10;
export const POLL_MAX_HZ = 50;

/** 连续多少次超时后判定设备 `stale`。 */
export const STALE_AFTER_FAILURES = 3;

/** 失联设备的轮询退避：`stale` 后每 1 s 再试一次，避免拖垮其他设备。 */
export const POLL_BACKOFF_MS = 1000;

/** 界面上限：桥接层不做单位换算也不做限幅，这些只是输入提示，不是固件保护值。 */
export const SOFT_RANGES = {
  /** 固件把 iq 参考钳在 ±MAX_ABS_CURRENT ≈ 3.64 A（`foc_driver.h`：1.6 V / EQ_GAIN 8.8 / 50 mΩ）。 */
  iqA: { min: -3.6, max: 3.6 },
  speedRadS: { min: -2048, max: 2048 },
  posRad: { min: -1_000_000, max: 1_000_000 },
  bandwidthHz: { min: 1, max: 20_000 },
  /** `SET_SPEED_ZETA` / `SET_ABFILT_ZETA`：小于 1 有超调，等于 1 临界阻尼。 */
  zeta: { min: 0.1, max: 2 },
  overCurrentA: { min: 0.1, max: 3.6 },
  overCurrentMs: { min: 1, max: 60_000 },
  baud: { min: 1200, max: 2_000_000 },
} as const;
