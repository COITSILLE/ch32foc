import { el } from './dom';

/** 日志行：只用结构类型，组件层不依赖 app 层的具体类型。 */
export interface LogRow {
  id: number;
  dir: 'tx' | 'rx' | 'err' | 'sys';
  addr: number | null;
  text: string;
}

export interface LogListOptions {
  /** 最多渲染多少条（环形，超上限丢最旧）。 */
  capacity?: number;
  onPauseChange?: (paused: boolean) => void;
}

export interface LogList {
  root: HTMLElement;
  /** 增量同步：只追加新行，避免每帧重建 DOM。 */
  update(entries: readonly LogRow[], paused: boolean): void;
}

const DIR_PREFIX: Record<LogRow['dir'], string> = {
  tx: '> ',
  rx: '< ',
  err: '! ',
  sys: '· ',
};

/**
 * 日志列表：只保留最近 `capacity` 条 DOM 节点。
 * 暂停时冻结渲染（数据继续缓冲），恢复后自动补齐——这样不会丢掉暂停期间的现场。
 */
export function createLogList(options: LogListOptions = {}): LogList {
  const capacity = options.capacity ?? 400;
  const list = el('div', { cls: 'log-body' });
  const pauseBtn = el('button', { cls: 'btn ghost', text: '暂停', attrs: { type: 'button' } });
  const root = el('div', { cls: 'log' }, [list, pauseBtn]);

  let lastId = 0;
  let paused = false;

  pauseBtn.addEventListener('click', () => {
    paused = !paused;
    pauseBtn.textContent = paused ? '继续' : '暂停';
    pauseBtn.classList.toggle('on', paused);
    options.onPauseChange?.(paused);
    if (!paused) lastId = 0;
  });

  const buildRow = (entry: LogRow): HTMLElement => {
    const prefix = entry.addr === null ? '' : `[${String.fromCharCode(65 + entry.addr)}] `;
    return el('div', { cls: `log-row ${entry.dir}`, text: DIR_PREFIX[entry.dir] + prefix + entry.text });
  };

  return {
    root,
    update(entries, isPaused) {
      if (isPaused) return;

      if (entries.length === 0) {
        if (list.childElementCount > 0) list.replaceChildren();
        lastId = 0;
        return;
      }

      const first = entries[0];
      if (first !== undefined && first.id <= lastId) {
        // 日志被清空或环形缓冲已经覆盖：整体重建可见窗口
        const visible = entries.slice(-capacity);
        list.replaceChildren(...visible.map(buildRow));
        lastId = visible[visible.length - 1]?.id ?? 0;
        list.scrollTop = list.scrollHeight;
        return;
      }

      const fresh = entries.filter((entry) => entry.id > lastId);
      if (fresh.length === 0) return;
      const frag = document.createDocumentFragment();
      for (const entry of fresh) frag.append(buildRow(entry));
      list.append(frag);
      while (list.childElementCount > capacity) {
        list.firstElementChild?.remove();
      }
      lastId = fresh[fresh.length - 1]?.id ?? lastId;
      list.scrollTop = list.scrollHeight;
    },
  };
}
