import type { LoopMode } from '../protocol/types';

export type LinkState = 'offline' | 'online' | 'stale';

/** `INFO` / `DADDR` / `BW` / `ZETA` / `OVC` / `BAUD` 读回来或写成功后填充的配置。 */
export interface DeviceConfig {
  firmwareVersion?: number;
  chipId?: number;
  deviceId?: number;
  deviceAddr?: number;
  baud?: number;
  overCurrentA?: number;
  overCurrentMs?: number;
  bandwidthHz?: Partial<Record<'CUR' | 'SPD' | 'POS' | 'ESO' | 'AB', number>>;
  zeta?: Partial<Record<'SPD' | 'AB', number>>;
}

export interface SysRegSnapshot {
  /** 原始 32 位寄存器：warning 在 [7:0]、fault 在 [15:8]、state 在 [23:16]。 */
  raw: number;
  state: number;
  warn: number;
  fault: number;
}

/** 单台设备的不可变快照，UI 直接渲染，不做二次加工。 */
export interface DeviceSnapshot {
  addr: number;
  name: string;
  link: LinkState;
  enabled: boolean;
  mode: LoopMode;
  /** 当前模式下的目标值（cur=A / spd=rad/s / pos=rad）。 */
  target: number;
  iqA: number;
  speedRadS: number;
  posRad: number;
  vbusV: number;
  sysreg: SysRegSnapshot;
  config: DeviceConfig;
  /** 最近一次成功收到回包的时刻（Clock 毫秒）。 */
  lastSeenMs: number;
  /** 连续超时次数，达到阈值后 link 变 stale。 */
  failures: number;
}

export function emptySnapshot(addr: number, name?: string): DeviceSnapshot {
  return {
    addr,
    name: name ?? `电机 ${String.fromCharCode(65 + addr)}`,
    link: 'offline',
    enabled: false,
    mode: 'cur',
    target: 0,
    iqA: 0,
    speedRadS: 0,
    posRad: 0,
    vbusV: 0,
    sysreg: { raw: 0, state: 0, warn: 0, fault: 0 },
    config: {},
    lastSeenMs: 0,
    failures: 0,
  };
}

export function isDeviceOnline(snapshot: DeviceSnapshot): boolean {
  return snapshot.link === 'online';
}
