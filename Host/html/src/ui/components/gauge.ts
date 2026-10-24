import { el, setText } from './dom';

export interface Gauge {
  root: HTMLElement;
  set(value: number, max: number, caption: string): void;
}

const RADIUS = 62;
const CIRCUMFERENCE = 2 * Math.PI * RADIUS;

/** 环形仪表：数值绝对居中，说明文字回归文档流（避免压环）。 */
export function createGauge(label: string, unit: string): Gauge {
  const arc = document.createElementNS('http://www.w3.org/2000/svg', 'circle');
  arc.setAttribute('cx', '75');
  arc.setAttribute('cy', '75');
  arc.setAttribute('r', String(RADIUS));
  arc.setAttribute('fill', 'none');
  arc.setAttribute('stroke-width', '10');
  arc.setAttribute('stroke-linecap', 'round');
  arc.setAttribute('stroke', 'var(--acc)');
  arc.setAttribute('transform', 'rotate(-90 75 75)');
  arc.setAttribute('stroke-dasharray', `0 ${CIRCUMFERENCE}`);

  const track = document.createElementNS('http://www.w3.org/2000/svg', 'circle');
  track.setAttribute('cx', '75');
  track.setAttribute('cy', '75');
  track.setAttribute('r', String(RADIUS));
  track.setAttribute('fill', 'none');
  track.setAttribute('stroke-width', '10');
  track.setAttribute('stroke', 'var(--line2)');

  const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  svg.setAttribute('viewBox', '0 0 150 150');
  svg.append(track, arc);

  const value = el('b', { text: '0.0' });
  const caption = el('span', { text: label });
  const number = el('div', { cls: 'gauge-num' }, [value, caption]);
  const scale = el('div', { cls: 'gauge-scale', text: `0 … ${unit}` });
  const root = el('div', { cls: 'gauge' }, [el('div', { cls: 'gauge-dial' }, [svg, number]), scale]);

  return {
    root,
    set(current, max, cap) {
      const ratio = max <= 0 ? 0 : Math.min(1, Math.max(0, Math.abs(current) / max));
      arc.setAttribute('stroke-dasharray', `${(CIRCUMFERENCE * ratio).toFixed(2)} ${CIRCUMFERENCE}`);
      setText(value, current.toFixed(1));
      setText(caption, cap);
    },
  };
}
