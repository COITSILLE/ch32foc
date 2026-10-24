import type { AppState } from '../../../app/state';
import type { AppActions } from '../../../app/actions';
import { fmtHex } from '../../../shared/fmt';
import { createCard } from '../../components/card';
import { button, el, setText } from '../../components/dom';

export interface CalibrationPanel {
  root: HTMLElement;
  update(state: AppState): void;
}

export function createCalibrationPanel(actions: AppActions): CalibrationPanel {
  let state: AppState | null = null;
  const target = (): number => state?.debug.target ?? 0;
  const unlocked = (): boolean => state?.debug.unlocked === true && state.phase === 'connected';

  const card = createCard('校准 / 设备信息', '校准期间固件阻塞约 1.1 s，不会回包，界面按 1.5 s 放过');

  const infoButton = button('读取设备信息', () => void actions.readInfo(target()), 'btn primary');
  const infoText = el('div', { cls: 'kv mono', text: '未读取' });

  const calibButton = button('零点校准', () => void actions.calibrate(target()), 'btn');
  const calibText = el('span', { cls: 'muted', text: '需要母线电压 > 5 V，电机可自由转动' });

  const addrButton = button('读取设备地址', () => void actions.readDeviceAddr(target()), 'btn ghost');
  const addrText = el('div', { cls: 'kv mono', text: '只读（上电后转动电机由编码器 UI 修改）' });

  card.body.append(
    el('div', { cls: 'row wrap gap' }, [infoButton, infoText]),
    el('div', { cls: 'row wrap gap' }, [calibButton, calibText]),
    el('div', { cls: 'row wrap gap' }, [addrButton, addrText]),
  );

  let lastKey = '';

  return {
    root: card.root,
    update(next) {
      state = next;
      const device = next.devices[next.debug.target];
      const busy = next.busyOperation !== null;
      infoButton.disabled = next.phase !== 'connected' || busy;
      calibButton.disabled = !unlocked() || busy;
      addrButton.disabled = !unlocked() || busy;
      setText(calibButton, busy ? '校准中…' : '零点校准');

      const config = device?.config;
      const key = `${next.debug.target}:${JSON.stringify(config ?? {})}:${device?.config.deviceAddr ?? -1}:${busy}:${next.debug.unlocked}`;
      if (key === lastKey) return;
      lastKey = key;

      if (config?.firmwareVersion !== undefined) {
        setText(
          infoText,
          `固件 ${fmtHex(config.firmwareVersion, 6)} · 芯片 ${fmtHex(config.chipId ?? 0, 6)} · 设备 ${fmtHex(config.deviceId ?? 0, 6)}`,
        );
      } else {
        setText(infoText, '未读取');
      }

      setText(
        addrText,
        device?.config.deviceAddr === undefined
          ? '只读（上电后转动电机由编码器 UI 修改）'
          : `设备地址 ${device.config.deviceAddr}`,
      );

      if (busy) {
        setText(calibText, next.busyOperation ?? '');
      } else if (next.debug.unlocked) {
        setText(calibText, 'DEBUG 已解锁');
      } else {
        setText(calibText, '需要母线电压 > 5 V，电机可自由转动');
      }
    },
  };
}
