import type { LoopMode } from '../core/protocol/types';
import type { DeviceSnapshot } from '../core/domain/device';
import type { AppError } from '../core/app-error';

export type Phase = 'idle' | 'connecting' | 'connected';

export type LogDirection = 'tx' | 'rx' | 'err' | 'sys';

export interface LogEntry {
  id: number;
  /** Clock 毫秒。 */
  tMs: number;
  dir: LogDirection;
  addr: number | null;
  text: string;
}

export interface ConnectionInfo {
  label: string;
  baudRate: number;
}

export type ChartMode = 'single' | 'compare';
export type ChartWindowSec = 5 | 10 | 30;

export interface DebugState {
  /** `INFO` 成功后解锁；桥接层用它判断是否真在 DEBUG 模式。 */
  unlocked: boolean;
  target: number;
}

export interface PatternState {
  kind: 'square' | 'sine';
  /** 波形作用的闭环模式：跟着开始时的模式走，不在中途改模式。 */
  mode: LoopMode;
  amplitude: number;
  /** 中心值：位置环用开始时的位置，其余为 0。 */
  center: number;
  startedAtMs: number;
}

export interface StepRecord {
  addr: number;
  from: number;
  to: number;
  tMs: number;
}

export interface AppState {
  phase: Phase;
  connection: ConnectionInfo | null;
  baudRate: number;
  activeAddr: number;
  devices: Record<number, DeviceSnapshot>;
  chartMode: ChartMode;
  chartWindowSec: ChartWindowSec;
  debug: DebugState;
  log: LogEntry[];
  logPaused: boolean;
  logVisible: boolean;
  /** 每台设备各自独立跟踪，互不干扰（键是地址）。 */
  patterns: Record<number, PatternState>;
  lastStep: StepRecord | null;
  pollEnabled: boolean;
  pollHz: number;
  /** 正在进行的阻塞操作（校准/存 Flash），用于给按钮上 pending 态。 */
  busyOperation: string | null;
  statusText: string;
  lastError: AppError | null;
  /** 已按命令上下文翻译过的错误文案（状态条用；避免重复算或算错上下文）。 */
  lastErrorText: string | null;
  toast: { id: number; text: string; tone: 'info' | 'warn' | 'error' } | null;
  startedAtMs: number | null;
}

export const LOG_CAPACITY = 2000;

export function initialState(): AppState {
  return {
    phase: 'idle',
    connection: null,
    baudRate: 115200,
    activeAddr: 0,
    devices: {},
    chartMode: 'single',
    chartWindowSec: 10,
    debug: { unlocked: false, target: 0 },
    log: [],
    logPaused: false,
    logVisible: true,
    patterns: {},
    lastStep: null,
    pollEnabled: false,
    pollHz: 0,
    busyOperation: null,
    statusText: '未连接',
    lastError: null,
    lastErrorText: null,
    toast: null,
    startedAtMs: null,
  };
}

export const MODE_LABEL: Readonly<Record<LoopMode, string>> = {
  cur: '电流',
  spd: '速度',
  pos: '位置',
};

export const MODE_UNIT: Readonly<Record<LoopMode, string>> = {
  cur: 'A',
  spd: 'rad/s',
  pos: 'rad',
};
