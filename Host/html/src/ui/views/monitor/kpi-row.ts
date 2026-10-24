import type { DeviceSnapshot } from '../../../core/domain/device';
import { UNIT_LABEL } from '../../../core/domain/units';
import { createStatCard, type StatCard } from '../../components/card';
import { el } from '../../components/dom';

export interface KpiRow {
  root: HTMLElement;
  update(device: DeviceSnapshot | null): void;
}

export function createKpiRow(): KpiRow {
  const iq = createStatCard('电流 Iq', UNIT_LABEL.iqA, 'var(--acc)');
  const speed = createStatCard('速度', UNIT_LABEL.speedRadS, 'var(--teal)');
  const position = createStatCard('位置', UNIT_LABEL.posRad, 'var(--violet)');
  const vbus = createStatCard('母线电压', UNIT_LABEL.vbusV, 'var(--warn)');
  const cards: StatCard[] = [iq, speed, position, vbus];
  const root = el('div', { cls: 'stats' }, cards.map((card) => card.root));

  return {
    root,
    update(device) {
      if (device === null) {
        for (const card of cards) card.set('--');
        return;
      }
      iq.set(device.iqA.toFixed(2), Math.abs(device.iqA) > 1.2 ? 'warn' : 'ok');
      speed.set(device.speedRadS.toFixed(1), 'ok');
      position.set(device.posRad.toFixed(2), 'dim');
      vbus.set(device.vbusV.toFixed(2), device.vbusV > 5 ? 'ok' : 'err');
    },
  };
}
