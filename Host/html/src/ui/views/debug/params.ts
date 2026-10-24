import type { AppState } from '../../../app/state';
import type { AppActions } from '../../../app/actions';
import type { BandwidthChannel, DampingChannel } from '../../../core/protocol/types';
import { SOFT_RANGES } from '../../../core/protocol/limits';
import { createCard } from '../../components/card';
import { createNumberField, type NumberField } from '../../components/number-field';
import { button, el, setText } from '../../components/dom';

export interface ParamsPanel {
  root: HTMLElement;
  update(state: AppState): void;
}

/** 默认值与固件 `flash_save_data.h` 的 `motor_cfg_default` 一致（协议没有回读，只能自己对齐）。 */
const CHANNELS: { which: BandwidthChannel; label: string; defaultValue: number }[] = [
  { which: 'CUR', label: '电流环', defaultValue: 400 },
  { which: 'SPD', label: '速度环', defaultValue: 20 },
  { which: 'POS', label: '位置环', defaultValue: 2 },
  { which: 'ESO', label: 'ESO', defaultValue: 200 },
  { which: 'AB', label: 'AB 滤波', defaultValue: 80 },
];

const DAMPING: { which: DampingChannel; label: string; defaultValue: number }[] = [
  { which: 'SPD', label: '速度环 ζ', defaultValue: 1.0 },
  { which: 'AB', label: 'AB 滤波 ζ', defaultValue: 0.9 },
];

export function createParamsPanel(actions: AppActions): ParamsPanel {
  let state: AppState | null = null;
  const target = (): number => state?.debug.target ?? 0;
  const connected = (): boolean => state?.phase === 'connected';

  /**
   * 门禁按命令分两档（依据固件 `app_main.cpp`）：
   *  - `BW` / `ZETA` 走的是 MCU 的普通命令号，**不需要 DEBUG**，连着就能写
   *  - `OVC` / `BAUD` / `DEFAULTS` / `SAVE` / `ZOFF` 是 `CmdDebug`，非 DEBUG 会回 `ERR DEBUG`
   */
  const liveWritable = (): boolean => connected();
  const debugWritable = (): boolean => connected() && state?.debug.unlocked === true;

  const card = createCard('参数', '写入只改 RAM，掉电恢复默认；要保留必须保存到 Flash');

  const bwFields = new Map<BandwidthChannel, NumberField>();
  const bwRows = CHANNELS.map(({ which, label, defaultValue }) => {
    const field = createNumberField({
      label,
      unit: 'Hz',
      value: defaultValue,
      min: SOFT_RANGES.bandwidthHz.min,
      max: SOFT_RANGES.bandwidthHz.max,
      step: 10,
    });
    bwFields.set(which, field);
    const write = button(
      '写',
      () => void actions.writeParams(target(), { bandwidth: { [which]: field.get() } }),
      'btn ghost',
    );
    return { row: el('div', { cls: 'row spread' }, [field.root, write]), write };
  });

  const zetaFields = new Map<DampingChannel, NumberField>();
  const zetaRows = DAMPING.map(({ which, label, defaultValue }) => {
    const field = createNumberField({
      label,
      value: defaultValue,
      min: SOFT_RANGES.zeta.min,
      max: SOFT_RANGES.zeta.max,
      step: 0.05,
    });
    zetaFields.set(which, field);
    const write = button(
      '写',
      () => void actions.writeParams(target(), { zeta: { [which]: field.get() } }),
      'btn ghost',
    );
    return { row: el('div', { cls: 'row spread' }, [field.root, write]), write };
  });

  const ovcAmps = createNumberField({
    label: '过流阈值',
    unit: 'A',
    value: 2,
    min: SOFT_RANGES.overCurrentA.min,
    max: SOFT_RANGES.overCurrentA.max,
    step: 0.1,
  });
  const ovcMs = createNumberField({
    label: '持续时间',
    unit: 'ms',
    value: 3000,
    min: SOFT_RANGES.overCurrentMs.min,
    max: SOFT_RANGES.overCurrentMs.max,
    step: 100,
  });
  const ovcButton = button(
    '写过流',
    () => void actions.writeParams(target(), { overCurrent: { amps: ovcAmps.get(), ms: ovcMs.get() } }),
    'btn ghost',
  );

  const baudField = createNumberField({
    label: '波特率',
    unit: '重启生效',
    value: 115200,
    min: SOFT_RANGES.baud.min,
    max: SOFT_RANGES.baud.max,
    step: 100,
  });
  const baudButton = button(
    '写波特率',
    () => void actions.writeParams(target(), { baud: baudField.get() }),
    'btn ghost',
  );

  const defaultsButton = button('恢复默认', () => void actions.writeDefaults(target()), 'btn ghost');
  const saveButton = button('保存到 Flash', () => void actions.save(target()), 'btn primary');

  const zeroOffsetButton = button('重算零点偏移', () => void actions.zeroOffset(target()), 'btn ghost');
  const zeroOffsetRow = el('div', { cls: 'row spread' }, [
    el('div', { cls: 'row gap' }, [
      el('span', { text: '零点偏移' }),
      el('span', { cls: 'hint', text: '以当前编码器位置为基准' }),
    ]),
    zeroOffsetButton,
  ]);

  const gateHint = el('p', {
    cls: 'hint',
    text: '带宽 / 阻尼比连接后即可写；过流、波特率、零点偏移、恢复默认、保存 Flash 需先「读取设备信息」解锁 DEBUG。',
  });

  card.body.append(
    el('div', { cls: 'bw-grid' }, [...bwRows.map((r) => r.row), ...zetaRows.map((r) => r.row)]),
    el('div', { cls: 'row wrap gap' }, [ovcAmps.root, ovcMs.root, ovcButton]),
    el('div', { cls: 'row wrap gap' }, [baudField.root, baudButton]),
    zeroOffsetRow,
    el('div', { cls: 'row wrap gap' }, [defaultsButton, saveButton]),
    gateHint,
  );

  let lastConfigKey = '';

  return {
    root: card.root,
    update(next) {
      state = next;
      const device = next.devices[next.debug.target];
      const live = liveWritable();
      const debugOk = debugWritable();

      for (const { write } of bwRows) write.disabled = !live;
      for (const { write } of zetaRows) write.disabled = !live;
      for (const btn of [ovcButton, baudButton, zeroOffsetButton, defaultsButton, saveButton]) {
        btn.disabled = !debugOk;
      }
      gateHint.hidden = debugOk;
      setText(saveButton, next.busyOperation === '保存到 Flash…' ? '保存中…' : '保存到 Flash');

      const config = device?.config;
      const configKey = `${next.debug.target}:${JSON.stringify(config ?? {})}`;
      if (configKey === lastConfigKey) return;
      lastConfigKey = configKey;

      for (const [which, field] of bwFields) {
        const value = config?.bandwidthHz?.[which];
        if (value !== undefined) field.set(value);
      }
      for (const [which, field] of zetaFields) {
        const value = config?.zeta?.[which];
        if (value !== undefined) field.set(value);
      }
      if (config?.overCurrentA !== undefined) ovcAmps.set(config.overCurrentA);
      if (config?.overCurrentMs !== undefined) ovcMs.set(config.overCurrentMs);
      if (config?.baud !== undefined) baudField.set(config.baud);
    },
  };
}
