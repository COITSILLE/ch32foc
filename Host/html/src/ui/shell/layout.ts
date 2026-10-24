import { el } from '../components/dom';

export interface Layout {
  root: HTMLElement;
  navMonitor: HTMLButtonElement;
  navDebug: HTMLButtonElement;
  deviceList: HTMLElement;
  addDeviceInput: HTMLInputElement;
  addDeviceButton: HTMLButtonElement;
  baudInput: HTMLInputElement;  connectButton: HTMLButtonElement;
  disconnectButton: HTMLButtonElement;
  connDot: HTMLElement;
  connText: HTMLElement;
  headerTitle: HTMLElement;
  headerSubtitle: HTMLElement;
  chipLink: HTMLElement;
  chipMode: HTMLElement;
  chipEnabled: HTMLElement;
  content: HTMLElement;
  statusbar: HTMLElement;
}

/** 侧边栏 + 内容区骨架；只搭 DOM，不读 store。 */
export function createLayout(mount: HTMLElement, footer: HTMLElement): Layout {
  const navMonitor = el('button', { cls: 'nav-item on', text: '监控', attrs: { type: 'button' } });
  const navDebug = el('button', { cls: 'nav-item', text: '调试', attrs: { type: 'button' } });
  const deviceList = el('div', { cls: 'device-list' });

  const addDeviceInput = el('input', {
    cls: 'num-input',
    attrs: { type: 'number', min: '0', max: '7', value: '1', title: '设备地址 0..7' },
  });
  const addDeviceButton = el('button', { cls: 'btn ghost', text: '添加', attrs: { type: 'button' } });

  const baudInput = el('input', {
    cls: 'num-input',
    attrs: { type: 'number', value: '115200', step: '100', title: '波特率（下位机默认 115200）' },
  });
  const connectButton = el('button', { cls: 'btn primary', text: '连接', attrs: { type: 'button' } });
  const disconnectButton = el('button', { cls: 'btn', text: '断开', attrs: { type: 'button', disabled: 'true' } });
  const connDot = el('span', { cls: 'dot' });
  const connText = el('span', { cls: 'conn-text', text: '未连接' });

  const sidebar = el('aside', { cls: 'side' }, [
    el('div', { cls: 'brand' }, [
      el('div', { cls: 'logo', text: 'FOC' }),
      el('div', {}, [
        el('b', { text: '2204 上位机' }),
        el('span', { text: 'MULTI-MOTOR CONSOLE' }),
      ]),
    ]),
    el('nav', { cls: 'nav' }, [navMonitor, navDebug]),
    el('div', { cls: 'side-section' }, [
      el('div', { cls: 'side-cap', text: '设备（总线地址）' }),
      deviceList,
      el('div', { cls: 'row inline' }, [addDeviceInput, addDeviceButton]),
      el('div', {
        cls: 'side-hint',
        text: '地址 0..7。列表里的一行就是一个寻址目标；改下位机自身地址要上电转电机再 SAVE。',
      }),
    ]),
    el('div', { cls: 'side-section side-card' }, [
      el('div', { cls: 'side-cap', text: '链路（USB 串口）' }),
      el('div', { cls: 'row inline' }, [baudInput]),
      el('div', { cls: 'row inline' }, [connectButton, disconnectButton]),
      el('div', { cls: 'conn-status' }, [connDot, connText]),
    ]),
    el('div', { cls: 'side-foot', text: 'USART2 · 115200 8N1 · ASCII 行协议' }),
  ]);

  const headerTitle = el('h1', { text: '未连接' });
  const headerSubtitle = el('p', { text: '连接后开始轮询' });
  const chipLink = el('span', { cls: 'chip' }, [el('i', { cls: 'dot' }), el('b', { text: '离线' })]);
  const chipMode = el('span', { cls: 'chip', text: '模式 --' });
  const chipEnabled = el('span', { cls: 'chip', text: '未知' });
  const content = el('div', { cls: 'content' });

  const header = el('header', { cls: 'head' }, [
    el('div', {}, [headerTitle, headerSubtitle]),
    el('div', { cls: 'head-right' }, [chipLink, chipMode, chipEnabled]),
  ]);

  const main = el('main', { cls: 'main' }, [header, content, footer]);
  const root = el('div', { cls: 'app' }, [sidebar, main]);
  mount.append(root);

  return {
    root,
    navMonitor,
    navDebug,
    deviceList,
    addDeviceInput,
    addDeviceButton,
    baudInput,
    connectButton,
    disconnectButton,
    connDot,
    connText,
    headerTitle,
    headerSubtitle,
    chipLink,
    chipMode,
    chipEnabled,
    content,
    statusbar: footer,
  };
}
