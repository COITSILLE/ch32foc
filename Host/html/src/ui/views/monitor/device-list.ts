import type { AppState } from '../../../app/state';
import type { AppActions } from '../../../app/actions';
import { faultShort, hasFault, linkLabel } from '../../../app/selectors';
import { UNIT_LABEL } from '../../../core/domain/units';
import { el, setClass, setText } from '../../components/dom';

export interface DeviceList {
  root: HTMLElement;
  update(state: AppState): void;
}

interface Row {
  root: HTMLElement;
  card: HTMLButtonElement;
  led: HTMLElement;
  name: HTMLElement;
  detail: HTMLElement;
  speed: HTMLElement;
}

/**
 * 设备列表完全由 `state.devices` 生成：加第三台不需要改这里。
 *
 * 一行 = 总线上的一个地址；选中即让桥接把后续命令发给该地址（`ADDR n` 由 Scheduler 自动补）。
 * 移除只是"不再显示/轮询这一台"，不会改下位机自己的地址。
 */
export function createDeviceList(actions: AppActions): DeviceList {
  const root = el('div', { cls: 'device-list' });
  const rows = new Map<number, Row>();
  let signature = '';

  return {
    root,
    update(state) {
      const devices = Object.values(state.devices).sort((a, b) => a.addr - b.addr);
      const nextSignature = devices.map((d) => d.addr).join(',');
      if (nextSignature !== signature) {
        signature = nextSignature;
        rows.clear();
        root.replaceChildren();
        for (const device of devices) {
          const led = el('span', { cls: 'dot' });
          const name = el('b', { text: device.name });
          const detail = el('small', { text: `地址 ${device.addr}` });
          const speed = el('b', { text: '0.0' });

          const card = el('button', { cls: 'device', attrs: { type: 'button' } }, [
            led,
            el('div', { cls: 'device-name' }, [name, detail]),
            el('div', { cls: 'device-value' }, [speed, el('i', { text: UNIT_LABEL.speedRadS })]),
          ]);
          card.addEventListener('click', () => actions.selectDevice(device.addr));

          const remove = el('button', {
            cls: 'device-remove',
            text: '×',
            title: `从列表移除地址 ${device.addr}（不改下位机地址）`,
            attrs: { type: 'button', 'aria-label': `移除设备 ${device.addr}` },
          });
          remove.addEventListener('click', (event) => {
            event.stopPropagation();
            actions.removeDevice(device.addr);
          });

          const row = el('div', { cls: 'device-row' }, [card, remove]);
          rows.set(device.addr, { root: row, card, led, name, detail, speed });
          root.append(row);
        }
      }

      for (const device of devices) {
        const row = rows.get(device.addr);
        if (row === undefined) continue;
        const tracking = state.patterns[device.addr] !== undefined;
        setClass(row.card, 'on', device.addr === state.activeAddr);
        setClass(row.card, 'tracking', tracking);
        setClass(row.led, 'live', device.link === 'online');
        setClass(row.led, 'err', hasFault(device));
        setText(row.name, device.name);
        const base =
          device.link !== 'online'
            ? `${linkLabel(device.link)} · ${faultShort(device)}`
            : hasFault(device)
              ? `地址 ${device.addr} · ${faultShort(device)}`
              : `地址 ${device.addr}`;
        setText(row.detail, tracking ? `${base} · 跟踪中` : base);
        setText(row.speed, device.speedRadS.toFixed(1));
      }
    },
  };
}
