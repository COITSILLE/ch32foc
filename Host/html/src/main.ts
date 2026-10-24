import './ui/theme/tokens.css';
import './ui/theme/base.css';

import { createStore } from './app/store';
import { initialState, MODE_LABEL, type AppState } from './app/state';
import { AppActions } from './app/actions';
import { linkLabel, hasFault, faultText } from './app/selectors';
import { systemClock } from './adapters/clock/system';
import { createConsoleLogger } from './adapters/logger/console';
import { WebSerialTransport } from './adapters/serial/web-serial';
import type { Transport } from './ports/transport';
import { createLayout } from './ui/shell/layout';
import { createRouter } from './ui/shell/router';
import { createStatusbar } from './ui/shell/statusbar';
import { createToast } from './ui/components/toast';
import { createDeviceList } from './ui/views/monitor/device-list';
import { createMonitorView } from './ui/views/monitor/index';
import { createDebugView } from './ui/views/debug/index';
import { el, setClass, setText } from './ui/components/dom';

const mount = document.getElementById('app');
if (mount === null) throw new Error('#app 挂载点不存在');

const store = createStore<AppState>(initialState());
const logger = createConsoleLogger('info');

const actions = new AppActions({
  store,
  clock: systemClock,
  logger,
  createTransport: (): Transport => new WebSerialTransport(),
});

const statusbar = createStatusbar();
const layout = createLayout(mount, statusbar.root);
const toast = createToast();
mount.append(toast.root);

const deviceList = createDeviceList(actions);
layout.deviceList.append(deviceList.root);

const monitor = createMonitorView(actions, systemClock, store);
const debug = createDebugView(actions);
layout.content.append(monitor.root, debug.root);

createRouter(
  { monitor: layout.navMonitor, debug: layout.navDebug },
  { monitor: monitor.root, debug: debug.root },
);

// 侧边栏交互
layout.connectButton.addEventListener('click', () => {
  const baudRate = Number(layout.baudInput.value);
  if (Number.isFinite(baudRate) && baudRate >= 1200) actions.setBaudRate(baudRate);
  void actions.connect();
});
layout.disconnectButton.addEventListener('click', () => {
  void actions.disconnect();
});
layout.addDeviceButton.addEventListener('click', () => {
  submitDeviceAddress();
});
layout.addDeviceInput.addEventListener('keydown', (event) => {
  if ((event as KeyboardEvent).key === 'Enter') submitDeviceAddress();
});

function submitDeviceAddress(): void {
  const addr = Number(layout.addDeviceInput.value);
  if (!Number.isInteger(addr) || addr < 0 || addr > 7) {
    toast.show('设备地址必须是 0..7 的整数', 'warn');
    return;
  }
  actions.addDevice(addr);
}
layout.baudInput.addEventListener('change', () => {
  const value = Number(layout.baudInput.value);
  if (Number.isFinite(value) && value >= 1200) actions.setBaudRate(value);
});

let scheduled = false;
let lastToastId = 0;
/** 只支持 Web Serial（Chrome / Edge）。 */
const serialSupported = navigator.serial !== undefined;

function render(): void {
  scheduled = false;
  const state = store.getState();
  const stats = actions.schedulerRef?.stats ?? {
    tx: 0,
    timeouts: 0,
    queueDepth: 0,
    inFlightAddr: null,
    perDeviceHz: 0,
    polling: false,
  };

  deviceList.update(state);
  monitor.update(state);
  debug.update(state);
  statusbar.update(state, stats);

  const device = state.devices[state.activeAddr] ?? null;
  setText(layout.headerTitle, device === null ? '未连接' : `${device.name} · 地址 ${device.addr}`);
  const fault = device !== null && hasFault(device) ? ` · ${faultText(device)}` : '';
  setText(
    layout.headerSubtitle,
    device === null
      ? '连接后开始轮询'
      : `${MODE_LABEL[device.mode]}环 · ${device.enabled ? '已使能' : '未使能'} · ${linkLabel(device.link)}${fault}`,
  );

  const dot = layout.chipLink.querySelector('.dot');
  if (dot !== null) {
    setClass(dot, 'ok', device?.link === 'online');
    setClass(dot, 'err', device === null || device.link !== 'online' || hasFault(device));
  }
  const linkText = layout.chipLink.querySelector('b');
  if (linkText !== null) setText(linkText, device === null ? '离线' : linkLabel(device.link));
  setText(layout.chipMode, `模式 ${device === null ? '--' : MODE_LABEL[device.mode]}`);
  setText(layout.chipEnabled, device === null ? '未知' : device.enabled ? '已使能' : '未使能');

  layout.connectButton.disabled = state.phase !== 'idle' || !serialSupported;
  layout.disconnectButton.disabled = state.phase === 'idle';
  layout.baudInput.disabled = state.phase !== 'idle';
  setText(layout.connText, state.connection?.label ?? (state.phase === 'connecting' ? '连接中…' : '未连接'));
  setClass(layout.connDot, 'ok', state.phase === 'connected');
  setClass(layout.connDot, 'err', state.phase === 'idle' && state.lastError !== null);

  if (state.toast !== null && state.toast.id !== lastToastId) {
    lastToastId = state.toast.id;
    toast.show(state.toast.text, state.toast.tone);
  }
}

function scheduleRender(): void {
  if (scheduled) return;
  scheduled = true;
  requestAnimationFrame(render);
}

store.subscribe(scheduleRender);
render();

// 浏览器不支持时直接禁用连接并说明原因，不给"假数据"入口
if (!serialSupported) {
  const hint = el('p', {
    cls: 'hint',
    text: '当前浏览器不支持 Web Serial，请改用 Chrome / Edge 打开本页',
  });
  layout.connectButton.parentElement?.after(hint);
}

logger.info('上位机已启动');
