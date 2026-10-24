import { el } from './dom';

export type ToastTone = 'info' | 'warn' | 'error';

export interface Toast {
  root: HTMLElement;
  show(text: string, tone: ToastTone): void;
}

export function createToast(): Toast {
  const root = el('div', { cls: 'toast', attrs: { role: 'status' } });
  let timer: ReturnType<typeof setTimeout> | null = null;

  return {
    root,
    show(text, tone) {
      root.textContent = text;
      root.dataset.tone = tone;
      root.classList.add('show');
      if (timer !== null) globalThis.clearTimeout(timer);
      timer = globalThis.setTimeout(() => {
        root.classList.remove('show');
      }, 4000);
    },
  };
}
