import type { AppState } from '../../../app/state';
import type { Sample } from '../../../core/domain/metrics';
import { stepMetrics } from '../../../core/domain/metrics';
import { UNIT_LABEL } from '../../../core/domain/units';
import { createCard } from '../../components/card';
import { el, setText } from '../../components/dom';

export interface Summary {
  root: HTMLElement;
  update(state: AppState, history: Map<number, Sample[]>): void;
}

interface Row {
  root: HTMLElement;
  value: HTMLElement;
}

const ANALYSIS_WINDOW_MS = 5000;

function row(label: string): Row {
  const value = el('b', { text: '--' });
  return { root: el('div', { cls: 'summary-row' }, [el('span', { text: label }), value]), value };
}

/** 阶跃指标来自 `core/domain/metrics` 的纯函数，UI 只负责显示。 */
export function createSummary(): Summary {
  const card = createCard('运行摘要', '以最近一次阶跃为准');
  const target = row('目标');
  const peak = row('峰值');
  const overshoot = row('超调');
  const rise = row('上升时间');
  const settling = row('调节时间(2%)');
  const steady = row('稳态误差');
  const rows = [target, peak, overshoot, rise, settling, steady];
  const hint = el('p', { cls: 'hint', text: '点「阶跃」后这里会显示指标' });

  card.body.append(el('div', { cls: 'summary-grid' }, rows.map((r) => r.root)), hint);

  return {
    root: card.root,
    update(state, history) {
      const step = state.lastStep;
      if (step === null) {
        hint.textContent = '点「阶跃」后这里会显示指标';
        hint.hidden = false;
        for (const r of rows) setText(r.value, '--');
        return;
      }

      // 只看阶跃后的一小段：否则用户事后停止/改目标会把指标带跑
      const windowEnd = step.tMs + ANALYSIS_WINDOW_MS;
      const samples = [
        { tMs: step.tMs, value: step.from },
        ...(history.get(step.addr) ?? []).filter((s) => s.tMs >= step.tMs && s.tMs <= windowEnd),
      ];
      const metrics = stepMetrics(samples, step.to);

      if (samples.length < 8) {
        hint.hidden = false;
        hint.textContent = `正在采集 ${state.devices[step.addr]?.name ?? ''} 的响应…`;
      } else {
        hint.hidden = true;
      }

      setText(target.value, `${step.to.toFixed(0)} ${UNIT_LABEL.speedRadS}`);
      setText(peak.value, `${metrics.peak.toFixed(1)} ${UNIT_LABEL.speedRadS}`);
      setText(overshoot.value, `${metrics.overshootPct.toFixed(1)} %`);
      setText(rise.value, metrics.riseMs === null ? '--' : `${metrics.riseMs.toFixed(0)} ms`);
      setText(settling.value, metrics.settlingMs === null ? '--' : `${metrics.settlingMs.toFixed(0)} ms`);
      setText(steady.value, `${metrics.steadyError.toFixed(2)} ${UNIT_LABEL.speedRadS}`);
    },
  };
}
