import { describe, expect, it } from 'vitest';
import { stepMetrics } from '../../src/core/domain/metrics';
import type { Sample } from '../../src/core/domain/metrics';

const STEP: Sample[] = [
  { tMs: 0, value: 0 },
  { tMs: 10, value: 10 },
  { tMs: 20, value: 50 },
  { tMs: 30, value: 90 },
  { tMs: 40, value: 110 },
  { tMs: 50, value: 120 },
  { tMs: 60, value: 115 },
  { tMs: 70, value: 105 },
  { tMs: 80, value: 101 },
  { tMs: 90, value: 100 },
  { tMs: 100, value: 100 },
  { tMs: 110, value: 100 },
];

describe('domain.metrics — 阶跃指标', () => {
  it('按 2% 判据算超调 / 上升时间 / 调节时间 / 稳态误差', () => {
    const m = stepMetrics(STEP, 100);
    expect(m.peak).toBe(120);
    expect(m.overshootPct).toBeCloseTo(20, 5);
    expect(m.riseMs).toBe(20);
    expect(m.settlingMs).toBe(70);
    expect(m.steadyError).toBeCloseTo(0, 6);
    expect(m.samples).toBe(STEP.length);
  });

  it('放宽到 5% 判据时调节时间提前', () => {
    expect(stepMetrics(STEP, 100, 0.05).settlingMs).toBe(60);
  });

  it('目标为 0 时不产生 NaN', () => {
    const samples: Sample[] = [
      { tMs: 0, value: 0 },
      { tMs: 10, value: 0.2 },
      { tMs: 20, value: 0 },
    ];
    const m = stepMetrics(samples, 0);
    expect(Number.isFinite(m.overshootPct)).toBe(true);
    expect(Number.isFinite(m.steadyError)).toBe(true);
  });

  it('空序列与非法目标返回零值', () => {
    expect(stepMetrics([], 100).samples).toBe(0);
    expect(stepMetrics(STEP, Number.NaN).samples).toBe(0);
  });

  it('采样分辨率不够时上升时间给 null，而不是假装 0 ms', () => {
    const coarse: Sample[] = [
      { tMs: 0, value: 0 },
      { tMs: 100, value: 100 },
      { tMs: 200, value: 100 },
    ];
    expect(stepMetrics(coarse, 100).riseMs).toBeNull();
  });

  it('反向阶跃同样能算出超调', () => {
    const samples: Sample[] = [
      { tMs: 0, value: 0 },
      { tMs: 10, value: -60 },
      { tMs: 20, value: -110 },
      { tMs: 30, value: -99 },
      { tMs: 40, value: -100 },
    ];
    const m = stepMetrics(samples, -100);
    expect(m.peak).toBe(-110);
    expect(m.overshootPct).toBeCloseTo(10, 5);
  });
});
