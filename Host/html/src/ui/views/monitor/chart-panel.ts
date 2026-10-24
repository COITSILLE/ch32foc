import type { AppState } from '../../../app/state';
import type { AppActions } from '../../../app/actions';
import { Plot, type PlotSeries } from '../../components/plot';
import { createTabs } from '../../components/tabs';
import { createCard } from '../../components/card';
import { el } from '../../components/dom';

export interface ChartPanel {
  root: HTMLElement;
  pushSample(tSec: number, values: Record<string, number | null>): void;
  update(state: AppState): void;
}

const DEVICE_COLORS = ['#2563eb', '#0d9e9e', '#f59e0b', '#a855f7', '#ec4899', '#0891b2', '#65a30d', '#dc2626'];
const TARGET_COLOR = '#94a3b8';

/**
 * 纵轴最小量程：左轴速度 30 rad/s，右轴 Iq 1 A。
 * 不设下限时，Iq 只有 0.01 A 也会把纵轴拉到 ±0.02，噪声看着像满量程震荡。
 */
const MIN_SPAN: [number, number] = [30, 1];

export function createChartPanel(actions: AppActions): ChartPanel {
  const canvas = el('canvas', { cls: 'plot-canvas' });
  const plot = new Plot(canvas, 10, { minSpan: MIN_SPAN });
  const card = createCard('曲线', '蓝/青：速度 · 虚线：目标 · 绿：Iq');

  const modeTabs = createTabs<AppState['chartMode']>(
    [
      { value: 'single', label: '单机' },
      { value: 'compare', label: '双机对比' },
    ],
    (value) => actions.setChartMode(value),
    'tabs compact',
  );
  const windowTabs = createTabs<string>(
    [
      { value: '5', label: '5 s' },
      { value: '10', label: '10 s' },
      { value: '30', label: '30 s' },
    ],
    (value) => actions.setChartWindow(Number(value) as AppState['chartWindowSec']),
    'tabs compact',
  );

  card.head.append(el('div', { cls: 'spacer' }), modeTabs.root, windowTabs.root);
  card.body.append(el('div', { cls: 'plot-wrap' }, [canvas]));

  let signature = '';

  return {
    root: card.root,
    pushSample(tSec, values) {
      plot.push(tSec, values);
    },
    update(state) {
      const devices = Object.values(state.devices).sort((a, b) => a.addr - b.addr);
      const next = `${state.chartMode}|${state.activeAddr}|${devices.map((d) => d.addr).join(',')}|${state.chartWindowSec}`;
      if (next === signature) return;
      signature = next;

      modeTabs.set(state.chartMode);
      windowTabs.set(String(state.chartWindowSec));
      plot.setWindow(state.chartWindowSec);

      if (state.chartMode === 'single') {
        const addr = state.activeAddr;
        const color = DEVICE_COLORS[addr % DEVICE_COLORS.length] ?? DEVICE_COLORS[0] ?? '#2563eb';
        plot.setSeries([
          { id: `d${addr}.speed`, label: '速度', color, axis: 0, decimals: 1 },
          { id: `d${addr}.target`, label: '目标', color: TARGET_COLOR, axis: 0, dashed: true, decimals: 1 },
          { id: `d${addr}.iq`, label: 'Iq', color: '#16a34a', axis: 1, decimals: 2 },
        ]);
        return;
      }

      const series: PlotSeries[] = [];
      for (const device of devices.slice(0, DEVICE_COLORS.length)) {
        const color = DEVICE_COLORS[device.addr % DEVICE_COLORS.length] ?? '#2563eb';
        series.push({ id: `d${device.addr}.speed`, label: `${device.name} 速度`, color, axis: 0, decimals: 1 });
        series.push({
          id: `d${device.addr}.target`,
          label: `${device.name} 目标`,
          color,
          axis: 0,
          dashed: true,
          decimals: 1,
        });
      }
      plot.setSeries(series);
    },
  };
}

export function frameValues(state: AppState): Record<string, number | null> {
  const values: Record<string, number | null> = {};
  for (const device of Object.values(state.devices)) {
    values[`d${device.addr}.speed`] = device.speedRadS;
    // 目标值只有速度环才和速度同量纲；其余模式画出来会误导
    values[`d${device.addr}.target`] = device.mode === 'spd' ? device.target : null;
    values[`d${device.addr}.iq`] = device.iqA;
  }
  return values;
}
