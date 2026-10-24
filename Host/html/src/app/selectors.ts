import type { AppState } from './state';
import { MODE_LABEL } from './state';
import type { DeviceSnapshot } from '../core/domain/device';
import { decodeSysReg, unknownBitsText } from '../core/domain/faults';
import { LOG_CAPACITY } from './state';

export function deviceList(state: AppState): DeviceSnapshot[] {
  return Object.values(state.devices).sort((a, b) => a.addr - b.addr);
}

export function activeDevice(state: AppState): DeviceSnapshot | null {
  return state.devices[state.activeAddr] ?? null;
}

export function deviceName(state: AppState, addr: number): string {
  return state.devices[addr]?.name ?? `电机 ${String.fromCharCode(65 + addr)}`;
}

export function modeLabel(state: AppState): string {
  const device = activeDevice(state);
  return device === null ? '--' : MODE_LABEL[device.mode];
}

export function linkLabel(link: DeviceSnapshot['link']): string {
  switch (link) {
    case 'online':
      return '在线';
    case 'stale':
      return '无应答';
    case 'offline':
      return '离线';
  }
}

/** 故障/告警的人话描述；正常设备返回"正常"。 */
export function faultText(device: DeviceSnapshot): string {
  const decoded = decodeSysReg(device.sysreg.state, device.sysreg.warn, device.sysreg.fault);
  const parts: string[] = [];
  if (decoded.faults.length > 0) parts.push(`故障：${decoded.faults.join('/')}`);
  if (decoded.warnings.length > 0) parts.push(`告警：${decoded.warnings.join('/')}`);
  const unknown = unknownBitsText(device.sysreg.warn, device.sysreg.fault);
  if (unknown !== null) parts.push(unknown);
  return parts.length > 0 ? parts.join('  ') : '正常';
}

export function hasFault(device: DeviceSnapshot): boolean {
  return device.sysreg.fault !== 0 || device.sysreg.state === 2;
}

/** 侧栏一行放不下长文案：只给第一个故障/告警的名字。 */
export function faultShort(device: DeviceSnapshot): string {
  const decoded = decodeSysReg(device.sysreg.state, device.sysreg.warn, device.sysreg.fault);
  if (decoded.faults.length > 0) return decoded.faults[0] ?? '故障';
  if (decoded.warnings.length > 0) return decoded.warnings[0] ?? '告警';
  if (device.sysreg.state === 2) return '故障';
  const unknown = unknownBitsText(device.sysreg.warn, device.sysreg.fault);
  return unknown === null ? '正常' : unknown;
}

/** 有任意设备处于故障/无应答时，状态条要亮起来。 */
export function worstDevice(state: AppState): DeviceSnapshot | null {
  const devices = deviceList(state);
  return (
    devices.find((d) => hasFault(d)) ??
    devices.find((d) => d.link !== 'online') ??
    devices[0] ??
    null
  );
}

export function recentLog(state: AppState, limit = 400) {
  return state.log.slice(Math.max(0, state.log.length - limit));
}

export function canDebugUnlock(state: AppState): boolean {
  return state.phase === 'connected';
}

export const MAX_LOG_RENDER = LOG_CAPACITY;
