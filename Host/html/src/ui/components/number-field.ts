import { el, labeled } from './dom';

export interface NumberFieldOptions {
  label: string;
  unit?: string;
  value: number;
  min?: number;
  max?: number;
  step?: number;
  /** 宽输入框（目标值用）。 */
  big?: boolean;
  onEnter?: () => void;
}

export interface NumberField {
  root: HTMLElement;
  input: HTMLInputElement;
  get(): number;
  set(value: number): void;
  setUnit(unit: string): void;
  setStep(step: number): void;
}

export function createNumberField(options: NumberFieldOptions): NumberField {
  const input = el('input', {
    cls: options.big === true ? 'num-input big' : 'num-input',
    attrs: {
      type: 'number',
      value: String(options.value),
      ...(options.min !== undefined ? { min: String(options.min) } : {}),
      ...(options.max !== undefined ? { max: String(options.max) } : {}),
      ...(options.step !== undefined ? { step: String(options.step) } : {}),
    },
  });
  if (options.onEnter !== undefined) {
    input.addEventListener('keydown', (event) => {
      if ((event as KeyboardEvent).key === 'Enter') options.onEnter?.();
    });
  }
  const root = labeled(options.label, input);
  const unitNode = el('span', { cls: 'unit', text: options.unit ?? '' });
  root.append(unitNode);

  return {
    root,
    input,
    get() {
      const value = Number(input.value);
      return Number.isFinite(value) ? value : 0;
    },
    set(value) {
      const text = String(value);
      if (input.value !== text) input.value = text;
    },
    setUnit(unit) {
      if (unitNode.textContent !== unit) unitNode.textContent = unit;
    },
    setStep(step) {
      const text = String(step);
      if (input.step !== text) input.step = text;
    },
  };
}
