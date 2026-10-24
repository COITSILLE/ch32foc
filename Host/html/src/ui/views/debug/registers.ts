import type { AppState } from '../../../app/state';
import { decodeSysReg, resetReasons, unknownBitsText } from '../../../core/domain/faults';
import { fmtHex } from '../../../shared/fmt';
import { createCard } from '../../components/card';
import { el, setText } from '../../components/dom';

export interface RegistersPanel {
  root: HTMLElement;
  update(state: AppState): void;
}

interface Row {
  root: HTMLElement;
  value: HTMLElement;
}

function row(label: string, mono = true): Row {
  const value = el('b', { cls: mono ? 'mono' : '', text: '--' });
  return { root: el('div', { cls: 'kv' }, [el('span', { text: label }), value]), value };
}

export function createRegistersPanel(): RegistersPanel {
  const card = createCard('系统寄存器', 'warning 在 [7:0]，fault 在 [15:8]，state 在 [23:16]，复位原因在 [31:24]');
  const raw = row('原始值');
  const state = row('状态');
  const warn = row('告警位');
  const fault = row('故障位');
  const reset = row('复位原因', false);
  const decoded = el('div', { cls: 'reg-decoded', text: '正常' });
  const extra = el('div', { cls: 'hint', text: '' });

  card.body.append(
    el('div', { cls: 'reg-grid' }, [raw.root, state.root, warn.root, fault.root]),
    el('div', { cls: 'reg-grid' }, [reset.root]),
    decoded,
    extra,
  );

  return {
    root: card.root,
    update(app) {
      const device = app.devices[app.debug.target];
      const reg = device?.sysreg ?? { raw: 0, state: 0, warn: 0, fault: 0 };
      setText(raw.value, fmtHex(reg.raw));
      const info = decodeSysReg(reg.state, reg.warn, reg.fault);
      setText(state.value, `${info.stateLabel} (${reg.state})`);
      setText(warn.value, fmtHex(reg.warn, 2));
      setText(fault.value, fmtHex(reg.fault, 2));

      const reasons = resetReasons(reg.raw);
      setText(reset.value, reasons.length > 0 ? reasons.join('/') : '--');

      const parts: string[] = [];
      if (info.faults.length > 0) parts.push(`故障：${info.faults.join('/')}`);
      if (info.warnings.length > 0) parts.push(`告警：${info.warnings.join('/')}`);
      setText(decoded, parts.length > 0 ? parts.join('  ') : '正常');
      decoded.dataset.tone = info.severity;

      const unknown = unknownBitsText(reg.warn, reg.fault);
      setText(extra, unknown ?? '');
      extra.hidden = unknown === null;
    },
  };
}
