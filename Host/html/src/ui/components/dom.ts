export interface ElOptions {
  cls?: string;
  text?: string;
  title?: string;
  attrs?: Record<string, string>;
  on?: Partial<Record<keyof HTMLElementEventMap, (event: Event) => void>>;
}

export type Child = Node | string | null | undefined;

export function el<K extends keyof HTMLElementTagNameMap>(
  tag: K,
  options: ElOptions = {},
  children: Child[] = [],
): HTMLElementTagNameMap[K] {
  const node = document.createElement(tag);
  if (options.cls !== undefined) node.className = options.cls;
  if (options.text !== undefined) node.textContent = options.text;
  if (options.title !== undefined) node.title = options.title;
  if (options.attrs !== undefined) {
    for (const [key, value] of Object.entries(options.attrs)) node.setAttribute(key, value);
  }
  if (options.on !== undefined) {
    for (const [event, handler] of Object.entries(options.on)) {
      if (handler !== undefined) node.addEventListener(event, handler as EventListener);
    }
  }
  for (const child of children) {
    if (child === null || child === undefined) continue;
    node.append(typeof child === 'string' ? document.createTextNode(child) : child);
  }
  return node;
}

/** 只在内容变化时写 DOM：50 Hz × N 台设备下，避免无意义的样式重算。 */
export function setText(node: HTMLElement, text: string): void {
  if (node.textContent !== text) node.textContent = text;
}

export function setClass(node: Element, name: string, on: boolean): void {
  node.classList.toggle(name, on);
}

export function clear(node: HTMLElement): void {
  while (node.firstChild !== null) node.removeChild(node.firstChild);
}

export function button(
  label: string,
  onClick: () => void,
  cls = 'btn',
): HTMLButtonElement {
  return el('button', {
    cls,
    text: label,
    attrs: { type: 'button' },
    on: { click: onClick },
  });
}

export function labeled(label: string, control: HTMLElement): HTMLElement {
  return el('label', { cls: 'field' }, [el('span', { cls: 'field-label', text: label }), control]);
}
