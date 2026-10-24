import { el } from './dom';

export interface Card {
  root: HTMLElement;
  body: HTMLElement;
  head: HTMLElement;
}

export function createCard(title?: string, subtitle?: string): Card {
  const head = el('div', { cls: 'card-head' });
  if (title !== undefined) head.append(el('h2', { text: title }));
  if (subtitle !== undefined) head.append(el('span', { cls: 'card-sub', text: subtitle }));
  const body = el('div', { cls: 'card-body' });
  const root = el('section', { cls: 'card' }, [head, body]);
  return { root, body, head };
}

/** 读数卡片：标题 + 大数字 + 单位。 */
export interface StatCard {
  root: HTMLElement;
  set(value: string, tone?: 'ok' | 'warn' | 'err' | 'dim'): void;
}

export function createStatCard(label: string, unit: string, accent: string): StatCard {
  const value = el('span', { cls: 'stat-value', text: '--' });
  const unitNode = el('small', { text: unit });
  const root = el('section', { cls: 'stat' }, [
    el('div', { cls: 'stat-key' }, [el('i', { attrs: { style: `background:${accent}` } }), label]),
    el('div', { cls: 'stat-num' }, [value, unitNode]),
  ]);
  return {
    root,
    set(text, tone = 'dim') {
      if (value.textContent !== text) value.textContent = text;
      root.dataset.tone = tone;
    },
  };
}
