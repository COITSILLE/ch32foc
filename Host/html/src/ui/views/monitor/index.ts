import type { AppState } from '../../../app/state';
import type { Store } from '../../../app/store';
import type { AppActions } from '../../../app/actions';
import type { Clock } from '../../../ports/clock';
import type { Sample } from '../../../core/domain/metrics';
import { createKpiRow } from './kpi-row';
import { createChartPanel, frameValues } from './chart-panel';
import { createControlPanel } from './control-panel';
import { createSummary } from './summary';
import { el } from '../../components/dom';

export interface MonitorView {
  root: HTMLElement;
  update(state: AppState): void;
  destroy(): void;
}

/** 采样间隔：50 Hz 轮询下 20 ms 刚好一个点，不做多余插值。 */
const SAMPLE_MIN_GAP_MS = 20;
const HISTORY_CAPACITY = 6000;

/**
 * 常规页：读数 + 曲线 + 控制 + 摘要；只读 store、只发 action。
 *
 * 采样挂在 store 订阅上而不是渲染循环里：渲染是 rAF 合批的（可能被节流），
 * 用它当采样时钟会让阶跃指标（上升时间/超调）被量化得忽高忽低。store 变更才对应真实回包。
 */
export function createMonitorView(
  actions: AppActions,
  clock: Clock,
  store: Store<AppState>,
): MonitorView {
  const kpi = createKpiRow();
  const chart = createChartPanel(actions);
  const control = createControlPanel(actions);
  const summary = createSummary();
  const history = new Map<number, Sample[]>();
  /** 每台设备各自限速：用全局时钟会把交替到达的两台回包互相挤掉。 */
  const lastSampleMs = new Map<number, number>();

  const root = el('div', { cls: 'view monitor' }, [
    kpi.root,
    el('div', { cls: 'grid2' }, [chart.root, control.root]),
    summary.root,
  ]);

  const sample = (state: AppState): void => {
    const now = clock.now();
    let fresh = false;
    for (const device of Object.values(state.devices)) {
      const last = lastSampleMs.get(device.addr) ?? 0;
      if (now - last < SAMPLE_MIN_GAP_MS) continue;
      lastSampleMs.set(device.addr, now);
      const samples = history.get(device.addr) ?? [];
      samples.push({ tMs: now, value: device.speedRadS });
      if (samples.length > HISTORY_CAPACITY) samples.splice(0, samples.length - HISTORY_CAPACITY);
      history.set(device.addr, samples);
      fresh = true;
    }
    if (fresh) chart.pushSample(now / 1000, frameValues(state));
  };

  const unsubscribe = store.subscribe(sample);

  return {
    root,
    update(state) {
      kpi.update(state.devices[state.activeAddr] ?? null);
      chart.update(state);
      control.update(state);
      summary.update(state, history);
    },
    destroy() {
      unsubscribe();
    },
  };
}
