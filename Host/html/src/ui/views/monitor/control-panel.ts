import type { AppState, PatternState } from '../../../app/state';
import type { AppActions } from '../../../app/actions';
import type { LoopMode } from '../../../core/protocol/types';
import { AMPLITUDE_PRESETS, AMPLITUDE_STEP, UNIT_LABEL } from '../../../core/domain/units';
import { createCard } from '../../components/card';
import { createGauge } from '../../components/gauge';
import { createNumberField } from '../../components/number-field';
import { createSwitch } from '../../components/switch';
import { createTabs } from '../../components/tabs';
import { button, el, setClass, setText } from '../../components/dom';

export interface ControlPanel {
  root: HTMLElement;
  update(state: AppState): void;
}

/** 全场统一 rad/s：输入框里的数值就是协议数值，不做显示换算。 */
const SPEED_UNIT = UNIT_LABEL.speedRadS;

export function createControlPanel(actions: AppActions): ControlPanel {
  let state: AppState | null = null;
  const addr = (): number => state?.activeAddr ?? 0;
  const connected = (): boolean => state?.phase === 'connected';

  const card = createCard('控制', '一条命令一个来回；未回的拍子会被跳过');

  const modeTabs = createTabs<LoopMode>(
    [
      { value: 'cur', label: '电流' },
      { value: 'spd', label: '速度' },
      { value: 'pos', label: '位置' },
    ],
    (mode) => void actions.setMode(addr(), mode),
    'modebar',
  );

  const enableSwitch = createSwitch((on) => void actions.enable(addr(), on));
  const enableRow = el('div', { cls: 'row spread' }, [
    modeTabs.root,
    el('div', { cls: 'row' }, [el('span', { cls: 'muted', text: '使能' }), enableSwitch.root]),
  ]);

  const gauge = createGauge('速度', SPEED_UNIT);

  /** 目标值的单位：速度环 rad/s，电流环 A，位置环 rad。 */
  const unitFor = (mode: LoopMode): string =>
    mode === 'spd' ? SPEED_UNIT : mode === 'cur' ? UNIT_LABEL.iqA : UNIT_LABEL.posRad;

  const targetField = createNumberField({
    label: '目标',
    unit: SPEED_UNIT,
    value: 0,
    step: 5,
    big: true,
    onEnter: () => void actions.setTarget(addr(), targetField.get()),
  });
  const applyButton = button('下发', () => void actions.setTarget(addr(), targetField.get()), 'btn primary');
  const stopButton = button(
    '停止',
    () => {
      void (async () => {
        await actions.setMode(addr(), 'cur');
        await actions.setTarget(addr(), 0);
      })();
    },
    'btn danger',
  );

  /** 阶跃/方波/正弦共用的幅值，单位随模式变化（rad/s / rad / A）。 */
  const amplitudeField = createNumberField({
    label: '幅值',
    unit: SPEED_UNIT,
    value: AMPLITUDE_PRESETS.spd,
    step: AMPLITUDE_STEP.spd,
    min: 0,
  });

  const stepButtons = [1, -1].map((direction) => {
    const btn = button(
      `阶跃 ${direction > 0 ? '↑' : '↓'}`,
      () => void actions.step(addr(), direction * amplitudeField.get()),
      'btn ghost',
    );
    btn.title = '速度环阶跃（用上面的幅值），用于看超调/调节时间';
    return btn;
  });

  const activePattern = (): PatternState | null => state?.patterns[addr()] ?? null;

  /** 同一个波形再点一次就停，省得去点「停止跟踪」。幅度取输入框当前值。 */
  const togglePattern = (kind: 'square' | 'sine'): void => {
    if (activePattern()?.kind === kind) actions.stopPattern(addr());
    else actions.playPattern(addr(), kind, amplitudeField.get());
  };

  const squareButton = button('方波', () => togglePattern('square'), 'btn ghost');
  const sineButton = button('正弦', () => togglePattern('sine'), 'btn ghost');
  const stopPatternButton = button('停止跟踪', () => actions.stopPattern(addr()), 'btn ghost');

  /** 按钮文案跟着模式与幅值走：方波 ±30 rad/s / 方波 ±0.5 rad / 方波 ±0.5 A。 */
  const waveformLabel = (name: string, mode: LoopMode, active: PatternState | null): string => {
    const amplitude = active?.amplitude ?? amplitudeField.get();
    const unit = mode === 'cur' ? UNIT_LABEL.iqA : mode === 'spd' ? UNIT_LABEL.speedRadS : UNIT_LABEL.posRad;
    return `${name} ±${amplitude} ${unit}`;
  };

  const refreshWaveLabels = (): void => {
    if (state === null) return;
    const mode = state.devices[state.activeAddr]?.mode ?? 'cur';
    const pattern = activePattern();
    setText(squareButton, waveformLabel('方波', pattern?.mode ?? mode, pattern));
    setText(sineButton, waveformLabel('正弦', pattern?.mode ?? mode, pattern));
  };
  amplitudeField.input.addEventListener('input', refreshWaveLabels);

  const pingButton = button('PING', () => void actions.ping(addr()), 'btn ghost');
  const clearButton = button('清故障', () => void actions.clearFault(addr()), 'btn ghost');
  const stopAllButton = button('全部急停', () => void actions.stopAll(), 'btn danger');

  const busyLabel = el('span', { cls: 'muted' });

  card.body.append(
    enableRow,
    el('div', { cls: 'control-main' }, [
      gauge.root,
      el('div', { cls: 'control-right' }, [
        el('div', { cls: 'row target-row' }, [targetField.root, applyButton, stopButton]),
        el('div', { cls: 'row wrap' }, [amplitudeField.root, ...stepButtons]),
        el('div', { cls: 'row wrap' }, [squareButton, sineButton, stopPatternButton]),
        el('div', { cls: 'row wrap' }, [pingButton, clearButton, stopAllButton, busyLabel]),
      ]),
    ]),
  );

  let lastAddr = -1;
  let lastModeKey = '';
  let lastTargetKey = '';
  let lastInputMode: LoopMode | null = null;

  return {
    root: card.root,
    update(next) {
      state = next;
      const device = next.devices[next.activeAddr] ?? null;
      const isConnected = connected() && device !== null;
      const mode = device?.mode ?? 'cur';

      modeTabs.set(mode);
      enableSwitch.set(device?.enabled ?? false);
      const trackingCount = Object.keys(next.patterns).length;
      setText(
        busyLabel,
        next.busyOperation ??
          (activePattern() !== null
            ? `跟踪中（共 ${trackingCount} 台）`
            : trackingCount > 0
              ? `${trackingCount} 台在跟踪`
              : ''),
      );

      const speed = device?.speedRadS ?? 0;
      const maxSpeed = Math.max(AMPLITUDE_PRESETS.spd * 1.5, Math.abs(speed) * 1.2);
      gauge.set(speed, maxSpeed, device?.enabled === true ? '运行' : device === null ? '无设备' : '待机');

      // 换模式或换设备：输入框的单位与数值都要跟着变
      const modeKey = `${next.activeAddr}:${mode}`;
      const inputModeChanged = lastInputMode !== mode;
      if (modeKey !== lastModeKey || inputModeChanged) {
        lastModeKey = modeKey;
        lastInputMode = mode;
        targetField.setUnit(unitFor(mode));
        targetField.set(device?.target ?? 0);
        // 幅值换了量纲（rad/s ↔ rad ↔ A），给该模式的默认值，别把 30 rad/s 当 30 rad 发出去
        amplitudeField.setUnit(unitFor(mode));
        amplitudeField.setStep(AMPLITUDE_STEP[mode]);
        amplitudeField.set(AMPLITUDE_PRESETS[mode]);
      }
      if (next.activeAddr !== lastAddr) {
        lastAddr = next.activeAddr;
        targetField.set(device?.target ?? 0);
      }
      // 目标变了（阶跃/停止/波形）就同步输入框；用户正在输入时不要抢焦点内容
      const targetKey = `${next.activeAddr}:${device?.target ?? 0}`;
      if (targetKey !== lastTargetKey && document.activeElement !== targetField.input) {
        lastTargetKey = targetKey;
        targetField.set(device?.target ?? 0);
      }

      for (const btn of [applyButton, stopButton, squareButton, sineButton, stopPatternButton, pingButton, clearButton, stopAllButton]) {
        btn.disabled = !isConnected;
      }
      // 阶跃是速度环的阶跃响应测试，其它模式下没有意义（幅值单位也对不上）
      for (const btn of stepButtons) btn.disabled = !isConnected || mode !== 'spd';

      const pattern = activePattern();
      refreshWaveLabels();
      setClass(squareButton, 'on', pattern?.kind === 'square');
      setClass(sineButton, 'on', pattern?.kind === 'sine');
    },
  };
}
