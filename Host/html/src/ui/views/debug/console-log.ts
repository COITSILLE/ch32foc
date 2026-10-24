import type { AppState } from '../../../app/state';
import type { AppActions } from '../../../app/actions';
import { createLogList, type LogList } from '../../components/log-list';
import { createCard } from '../../components/card';
import { button, el } from '../../components/dom';

export interface ConsoleLog {
  root: HTMLElement;
  update(state: AppState): void;
}

export function createConsoleLog(actions: AppActions): ConsoleLog {
  const card = createCard('收发日志', '暂停只冻结显示，数据继续缓冲');
  const list: LogList = createLogList({
    capacity: 400,
    onPauseChange: (paused) => actions.setLogPaused(paused),
  });
  const clearButton = button('清空', () => actions.clearLog(), 'btn ghost');
  card.head.append(el('div', { cls: 'spacer' }), clearButton);
  card.body.append(list.root);

  return {
    root: card.root,
    update(state) {
      list.update(state.log, state.logPaused);
      card.root.hidden = !state.logVisible;
    },
  };
}
