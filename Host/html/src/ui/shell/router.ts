export type ViewName = 'monitor' | 'debug';

export interface Router {
  go(name: ViewName): void;
  current(): ViewName;
}

export function createRouter(
  buttons: Record<ViewName, HTMLButtonElement>,
  panes: Record<ViewName, HTMLElement>,
): Router {
  let active: ViewName = 'monitor';

  const apply = (name: ViewName): void => {
    active = name;
    for (const key of Object.keys(panes) as ViewName[]) {
      const pane = panes[key];
      const button = buttons[key];
      pane.hidden = key !== name;
      button.classList.toggle('on', key === name);
    }
  };

  for (const key of Object.keys(buttons) as ViewName[]) {
    buttons[key].addEventListener('click', () => apply(key));
  }
  apply('monitor');

  return {
    go: apply,
    current: () => active,
  };
}
