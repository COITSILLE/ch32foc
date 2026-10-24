import type { AppState } from '../../app/state';
import { worstDevice } from '../../app/selectors';
import { fmtDuration } from '../../shared/fmt';
import { el, setText } from '../components/dom';

export interface Statusbar {
  root: HTMLElement;
  update(state: AppState, stats: { tx: number; timeouts: number; queueDepth: number; perDeviceHz: number }): void;
}

export function createStatusbar(): Statusbar {
  const link = el('span', { cls: 'status-item', text: '链路：未连接' });
  const throughput = el('span', { cls: 'status-item', text: '轮询 -- Hz' });
  const queue = el('span', { cls: 'status-item', text: '队列 0' });
  const uptime = el('span', { cls: 'status-item', text: '时长 --' });
  const message = el('span', { cls: 'status-message', text: '' });
  const root = el('footer', { cls: 'statusbar' }, [link, throughput, queue, uptime, message]);

  return {
    root,
    update(state, stats) {
      const devices = Object.values(state.devices);
      const online = devices.filter((d) => d.link === 'online').length;
      setText(link, `链路：${state.connection?.label ?? '未连接'}（${online}/${devices.length} 在线）`);

      setText(
        throughput,
        state.pollEnabled ? `轮询 ${stats.perDeviceHz} Hz/台 · TX ${stats.tx}` : `轮询已暂停 · TX ${stats.tx}`,
      );
      setText(queue, `队列 ${stats.queueDepth}${stats.timeouts > 0 ? ` · 超时 ${stats.timeouts}` : ''}`);

      const started = state.startedAtMs;
      setText(uptime, `时长 ${started === null ? '--' : fmtDuration(Date.now() - started)}`);

      const worst = worstDevice(state);
      const text =
        state.lastErrorText !== null
          ? state.lastErrorText
          : worst !== null && worst.link !== 'online'
            ? `设备 ${worst.addr} ${worst.link === 'stale' ? '无应答' : '离线'}`
            : state.statusText;
      setText(message, text);
      root.dataset.tone = state.lastError === null ? 'ok' : 'err';
    },
  };
}
