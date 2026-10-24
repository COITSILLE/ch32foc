export interface Sample {
  tMs: number;
  value: number;
}

export interface StepMetrics {
  target: number;
  peak: number;
  /** 超调百分比（相对目标幅值）。目标为 0 时用阶跃幅值作分母。 */
  overshootPct: number;
  /** 10% → 90% 上升时间；从未到过 90% 时为 null。 */
  riseMs: number | null;
  /** 最后一次超出 ±2% 带的时刻（相对第一个采样点）；一直在带内为 0。 */
  settlingMs: number | null;
  /** 末段均值与目标之差。 */
  steadyError: number;
  samples: number;
}

const EMPTY: StepMetrics = {
  target: 0,
  peak: 0,
  overshootPct: 0,
  riseMs: null,
  settlingMs: null,
  steadyError: 0,
  samples: 0,
};

/**
 * 阶跃响应指标（纯函数）。
 *
 * `bandRatio` 默认 2%，即教科书里的 2% 调节时间判据；目标为 0 时用阶跃幅值代替分母，
 * 否则整个指标都退化成分母为 0 的 NaN。
 */
export function stepMetrics(
  samples: readonly Sample[],
  target: number,
  bandRatio = 0.02,
): StepMetrics {
  if (samples.length === 0 || !Number.isFinite(target)) return EMPTY;

  const first = samples[0];
  if (first === undefined) return EMPTY;

  const t0 = first.tMs;
  const start = first.value;
  const amplitude = Math.abs(target - start);
  const scale = Math.abs(target) > 1e-9 ? Math.abs(target) : amplitude;
  if (scale <= 1e-9) {
    return {
      ...EMPTY,
      target,
      peak: start,
      steadyError: 0,
      samples: samples.length,
    };
  }

  const direction = Math.sign(target - start) || 1;
  let peak = start;
  for (const s of samples) {
    if (!Number.isFinite(s.value)) continue;
    if (direction > 0 ? s.value > peak : s.value < peak) peak = s.value;
  }
  const excess = (peak - target) * direction;
  const overshootPct = Math.max(0, (excess / scale) * 100);

  const band = Math.abs(scale) * bandRatio;
  let settlingMs: number | null = 0;
  for (const s of samples) {
    if (Math.abs(s.value - target) > band) settlingMs = s.tMs - t0;
  }

  const lo = start + (target - start) * 0.1;
  const hi = start + (target - start) * 0.9;
  const reached = (value: number): boolean =>
    direction > 0 ? value >= hi : value <= hi;
  let t10: number | null = null;
  let t90: number | null = null;
  for (const s of samples) {
    if (t10 === null) {
      const past10 = direction > 0 ? s.value >= lo : s.value <= lo;
      if (past10) t10 = s.tMs;
    }
    if (t90 === null && reached(s.value)) {
      t90 = s.tMs;
    }
  }
  const riseMs = t10 === null || t90 === null || t90 <= t10 ? null : t90 - t10;

  const tailCount = Math.max(1, Math.floor(samples.length * 0.1));
  let sum = 0;
  let count = 0;
  for (let i = samples.length - tailCount; i < samples.length; i += 1) {
    const s = samples[i];
    if (s === undefined || !Number.isFinite(s.value)) continue;
    sum += s.value;
    count += 1;
  }
  const steadyError = count > 0 ? sum / count - target : 0;

  return {
    target,
    peak,
    overshootPct: Number.isFinite(overshootPct) ? overshootPct : 0,
    riseMs,
    settlingMs,
    steadyError,
    samples: samples.length,
  };
}
