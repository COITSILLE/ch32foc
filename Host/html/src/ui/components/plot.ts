import { clamp } from '../../shared/fmt';

export interface PlotSeries {
  id: string;
  label: string;
  color: string;
  /** 0 = 左轴，1 = 右轴。 */
  axis: 0 | 1;
  dashed?: boolean;
  decimals?: number;
}

interface Frame {
  t: number;
  values: Record<string, number | null>;
}

interface Scale {
  min: number;
  max: number;
}

export interface PlotOptions {
  /** 每条轴的最小量程（`[左轴, 右轴]`）：数据很小时不要放大成噪声。 */
  minSpan?: [number, number];
}

const CAPACITY = 4096;
const PAD = { top: 26, right: 40, bottom: 18, left: 44 };

/**
 * Canvas 曲线引擎：环形缓冲 + rAF 合批。
 *
 * 历史点固定容量、`push` 覆盖最旧，不 `shift`、不新建数组——长时间运行内存不涨。
 */
export class Plot {
  private readonly canvas: HTMLCanvasElement;
  private readonly ctx: CanvasRenderingContext2D;
  private readonly buffer: (Frame | null)[] = new Array(CAPACITY).fill(null);
  private head = 0;
  private count = 0;
  private series: PlotSeries[] = [];
  private windowSec = 10;
  private raf = 0;
  private width = 0;
  private height = 0;
  private readonly minSpan: [number, number];
  private readonly resizeObserver: ResizeObserver | null;

  constructor(canvas: HTMLCanvasElement, windowSec = 10, options: PlotOptions = {}) {
    const ctx = canvas.getContext('2d');
    if (ctx === null) throw new Error('canvas 2d context 不可用');
    this.canvas = canvas;
    this.ctx = ctx;
    this.windowSec = windowSec;
    this.minSpan = options.minSpan ?? [1, 1];

    this.resizeObserver =
      typeof ResizeObserver === 'undefined' ? null : new ResizeObserver(() => this.resize());
    this.resizeObserver?.observe(canvas);
    this.resize();
    this.loop();
  }

  setSeries(series: PlotSeries[]): void {
    this.series = series;
  }

  setWindow(sec: number): void {
    this.windowSec = sec;
  }

  clear(): void {
    this.buffer.fill(null);
    this.head = 0;
    this.count = 0;
  }

  push(tSec: number, values: Record<string, number | null>): void {
    this.buffer[this.head] = { t: tSec, values };
    this.head = (this.head + 1) % CAPACITY;
    this.count = Math.min(this.count + 1, CAPACITY);
  }

  destroy(): void {
    cancelAnimationFrame(this.raf);
    this.resizeObserver?.disconnect();
  }

  private resize(): void {
    const dpr = globalThis.devicePixelRatio || 1;
    const rect = this.canvas.getBoundingClientRect();
    this.width = Math.max(1, Math.round(rect.width));
    this.height = Math.max(1, Math.round(rect.height));
    this.canvas.width = Math.round(this.width * dpr);
    this.canvas.height = Math.round(this.height * dpr);
    this.ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  }

  private loop = (): void => {
    this.draw();
    this.raf = requestAnimationFrame(this.loop);
  };

  private frameAt(index: number): Frame | null {
    const start = (this.head - this.count + CAPACITY) % CAPACITY;
    return this.buffer[(start + index) % CAPACITY] ?? null;
  }

  private draw(): void {
    const { ctx, width: w, height: h } = this;
    ctx.clearRect(0, 0, w, h);

    const plotW = Math.max(1, w - PAD.left - PAD.right);
    const plotH = Math.max(1, h - PAD.top - PAD.bottom);
    const last = this.count > 0 ? this.frameAt(this.count - 1) : null;
    const now = last?.t ?? 0;
    const t0 = now - this.windowSec;

    ctx.save();
    ctx.beginPath();
    ctx.rect(PAD.left, PAD.top, plotW, plotH);
    ctx.clip();

    // 网格
    ctx.strokeStyle = 'rgba(16,32,54,.07)';
    ctx.lineWidth = 1;
    for (let i = 0; i <= 4; i += 1) {
      const y = PAD.top + (plotH * i) / 4;
      ctx.beginPath();
      ctx.moveTo(PAD.left, y);
      ctx.lineTo(PAD.left + plotW, y);
      ctx.stroke();
    }
    const tickSec = this.windowSec <= 5 ? 1 : this.windowSec <= 10 ? 2 : 5;
    for (let s = 0; s <= this.windowSec; s += tickSec) {
      const x = PAD.left + (s / this.windowSec) * plotW;
      ctx.beginPath();
      ctx.moveTo(x, PAD.top);
      ctx.lineTo(x, PAD.top + plotH);
      ctx.stroke();
    }

    const scale0 = this.computeScale(0, t0);
    const scale1 = this.computeScale(1, t0);

    this.drawZeroLine(scale0, plotH, plotW);

    for (const s of this.series) {
      this.drawSeries(s, t0, plotW, plotH, s.axis === 0 ? scale0 : scale1);
    }
    ctx.restore();

    this.drawAxisLabels(scale0, scale1, plotH, plotW);
    this.drawTimeLabels(plotW, plotH);
    this.drawLegend();
  }

  private computeScale(axis: 0 | 1, t0: number): Scale {
    let min = Infinity;
    let max = -Infinity;
    const ids = this.series.filter((s) => s.axis === axis).map((s) => s.id);
    if (ids.length === 0) return { min: -1, max: 1 };

    for (let i = 0; i < this.count; i += 1) {
      const frame = this.frameAt(i);
      if (frame === null || frame.t < t0) continue;
      for (const id of ids) {
        const value = frame.values[id];
        if (value === null || value === undefined || !Number.isFinite(value)) continue;
        if (value < min) min = value;
        if (value > max) max = value;
      }
    }
    if (!Number.isFinite(min) || !Number.isFinite(max)) return { min: -1, max: 1 };

    if (min > 0) min = 0;
    if (max < 0) max = 0;

    // 数据很小时保持一个"够看"的最小量程，否则 0.01 A 的噪声会被撑满整个纵轴
    const minSpan = this.minSpan[axis];
    if (max - min < minSpan) {
      const span = Math.max(minSpan, 1e-6);
      const center = (max + min) / 2;
      min = center - span / 2;
      max = center + span / 2;
    }

    const pad = (max - min) * 0.08;
    return { min: min - pad, max: max + pad };
  }

  private drawZeroLine(scale: Scale, plotH: number, plotW: number): void {
    if (scale.min > 0 || scale.max < 0) return;
    const y = PAD.top + plotH - ((0 - scale.min) / (scale.max - scale.min)) * plotH;
    this.ctx.strokeStyle = 'rgba(16,32,54,.18)';
    this.ctx.beginPath();
    this.ctx.moveTo(PAD.left, y);
    this.ctx.lineTo(PAD.left + plotW, y);
    this.ctx.stroke();
  }

  private drawSeries(series: PlotSeries, t0: number, plotW: number, plotH: number, scale: Scale): void {
    const { ctx } = this;
    ctx.strokeStyle = series.color;
    ctx.lineWidth = series.dashed === true ? 1.2 : 1.7;
    ctx.setLineDash(series.dashed === true ? [4, 4] : []);
    ctx.beginPath();

    let started = false;
    const span = Math.max(1e-6, this.windowSec);
    for (let i = 0; i < this.count; i += 1) {
      const frame = this.frameAt(i);
      if (frame === null || frame.t < t0) continue;
      const value = frame.values[series.id];
      if (value === null || value === undefined || !Number.isFinite(value)) {
        started = false;
        continue;
      }
      const x = PAD.left + ((frame.t - t0) / span) * plotW;
      const y = PAD.top + plotH - ((value - scale.min) / (scale.max - scale.min)) * plotH;
      if (started) {
        ctx.lineTo(x, clamp(y, PAD.top - 2, PAD.top + plotH + 2));
      } else {
        ctx.moveTo(x, clamp(y, PAD.top - 2, PAD.top + plotH + 2));
        started = true;
      }
    }
    ctx.stroke();
    ctx.setLineDash([]);
  }

  private drawAxisLabels(scale0: Scale, scale1: Scale, plotH: number, plotW: number): void {
    const { ctx } = this;
    ctx.fillStyle = 'rgba(23,35,49,.55)';
    ctx.font = '11px ui-monospace, Consolas, monospace';
    ctx.textAlign = 'right';
    ctx.textBaseline = 'middle';
    for (let i = 0; i <= 4; i += 1) {
      const ratio = 1 - i / 4;
      const y = PAD.top + plotH * (i / 4);
      const v0 = scale0.min + (scale0.max - scale0.min) * ratio;
      ctx.fillText(formatTick(v0), PAD.left - 6, y);
      if (this.series.some((s) => s.axis === 1)) {
        const v1 = scale1.min + (scale1.max - scale1.min) * ratio;
        ctx.textAlign = 'left';
        ctx.fillText(formatTick(v1), PAD.left + plotW + 6, y);
        ctx.textAlign = 'right';
      }
    }
  }

  private drawTimeLabels(plotW: number, plotH: number): void {
    const { ctx } = this;
    ctx.fillStyle = 'rgba(23,35,49,.42)';
    ctx.font = '10.5px ui-monospace, Consolas, monospace';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'top';
    const tickSec = this.windowSec <= 5 ? 1 : this.windowSec <= 10 ? 2 : 5;
    for (let s = 0; s <= this.windowSec; s += tickSec) {
      const x = PAD.left + (s / this.windowSec) * plotW;
      const label = s === this.windowSec ? '现在' : `-${this.windowSec - s}s`;
      ctx.fillText(label, x, PAD.top + plotH + 4);
    }
  }

  private drawLegend(): void {
    const { ctx } = this;
    ctx.font = '11px system-ui, "Segoe UI", sans-serif';
    ctx.textAlign = 'left';
    ctx.textBaseline = 'middle';
    let x = PAD.left;
    const last = this.count > 0 ? this.frameAt(this.count - 1) : null;
    for (const series of this.series) {
      ctx.strokeStyle = series.color;
      ctx.lineWidth = 2;
      ctx.beginPath();
      ctx.moveTo(x, 12);
      ctx.lineTo(x + 14, 12);
      ctx.stroke();
      const value = last?.values[series.id] ?? null;
      const text =
        value === null || value === undefined
          ? series.label
          : `${series.label} ${value.toFixed(series.decimals ?? 1)}`;
      ctx.fillStyle = 'rgba(23,35,49,.62)';
      ctx.fillText(text, x + 19, 12);
      x += ctx.measureText(text).width + 34;
    }
  }
}

function formatTick(value: number): string {
  const abs = Math.abs(value);
  if (abs >= 1000) return value.toFixed(0);
  if (abs >= 100) return value.toFixed(0);
  if (abs >= 10) return value.toFixed(1);
  return value.toFixed(2);
}
