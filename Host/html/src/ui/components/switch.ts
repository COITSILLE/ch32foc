import { el, setClass } from './dom';

export interface Toggle {
  root: HTMLElement;
  set(on: boolean): void;
}

export function createSwitch(onChange: (on: boolean) => void): Toggle {
  let on = false;
  const root = el('button', {
    cls: 'switch',
    attrs: { type: 'button', role: 'switch', 'aria-checked': 'false' },
    title: '使能 / 断使能',
  }, [el('i')]);
  root.addEventListener('click', () => {
    onChange(!on);
  });
  return {
    root,
    set(next) {
      on = next;
      setClass(root, 'on', next);
      root.setAttribute('aria-checked', next ? 'true' : 'false');
    },
  };
}
