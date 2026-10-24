import type { AppState } from '../../../app/state';
import type { AppActions } from '../../../app/actions';
import { createParamsPanel } from './params';
import { createCalibrationPanel } from './calibration';
import { createRegistersPanel } from './registers';
import { createConsoleLog } from './console-log';
import { el, setText } from '../../components/dom';

export interface DebugView {
  root: HTMLElement;
  update(state: AppState): void;
}

/** 调试页：参数、校准、寄存器、日志。所有命令都要求先 INFO 解锁。 */
export function createDebugView(actions: AppActions): DebugView {
  const params = createParamsPanel(actions);
  const calibration = createCalibrationPanel(actions);
  const registers = createRegistersPanel();
  const consoleLog = createConsoleLog(actions);

  const targetSelect = el('select', { cls: 'select' });
  const lockChip = el('span', { cls: 'chip', text: '未解锁' });
  targetSelect.addEventListener('change', () => {
    actions.selectDebugTarget(Number(targetSelect.value));
  });

  const root = el('div', { cls: 'view debug' }, [
    el('div', { cls: 'debug-toolbar' }, [
      el('span', { cls: 'muted', text: '调试对象' }),
      targetSelect,
      lockChip,
    ]),
    el('div', { cls: 'grid2 debug-grid' }, [
      el('div', { cls: 'stack' }, [calibration.root, registers.root]),
      params.root,
    ]),
    consoleLog.root,
  ]);

  let signature = '';

  return {
    root,
    update(state) {
      const devices = Object.values(state.devices).sort((a, b) => a.addr - b.addr);
      const next = `${devices.map((d) => d.addr).join(',')}|${state.debug.target}`;
      if (next !== signature) {
        signature = next;
        targetSelect.replaceChildren(
          ...devices.map((d) => el('option', { text: `${d.name}（地址 ${d.addr}）`, attrs: { value: String(d.addr) } })),
        );
        targetSelect.value = String(state.debug.target);
      }

      setText(lockChip, state.debug.unlocked ? 'DEBUG 已解锁' : '未解锁');
      lockChip.dataset.tone = state.debug.unlocked ? 'ok' : 'dim';

      params.update(state);
      calibration.update(state);
      registers.update(state);
      consoleLog.update(state);
    },
  };
}
