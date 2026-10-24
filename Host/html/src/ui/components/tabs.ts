import { el, setClass } from './dom';

export interface TabItem<T extends string> {
  value: T;
  label: string;
}

export interface Tabs<T extends string> {
  root: HTMLElement;
  set(value: T): void;
}

export function createTabs<T extends string>(
  items: readonly TabItem<T>[],
  onChange: (value: T) => void,
  cls = 'tabs',
): Tabs<T> {
  const buttons = items.map((item) => el('button', { cls: 'tab', text: item.label, attrs: { type: 'button' } }));
  const root = el('div', { cls }, buttons);
  let current = items[0]?.value;

  const select = (value: T): void => {
    current = value;
    buttons.forEach((btn, i) => setClass(btn, 'on', items[i]?.value === value));
  };

  buttons.forEach((btn, i) => {
    btn.addEventListener('click', () => {
      const item = items[i];
      if (item === undefined) return;
      select(item.value);
      onChange(item.value);
    });
  });

  if (current !== undefined) select(current);

  return {
    root,
    set(value) {
      if (value !== current) select(value);
    },
  };
}
