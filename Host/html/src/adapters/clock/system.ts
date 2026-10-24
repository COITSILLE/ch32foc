import type { Clock } from '../../ports/clock';

/** 系统时钟：唯一直接读 `Date.now()` / 定时器的地方。 */
export const systemClock: Clock = {
  now: () => Date.now(),
  setTimeout: (fn, ms) => {
    const id = globalThis.setTimeout(fn, ms);
    return id as unknown as number;
  },
  clearTimeout: (id) => {
    globalThis.clearTimeout(id);
  },
  setInterval: (fn, ms) => {
    const id = globalThis.setInterval(fn, ms);
    return id as unknown as number;
  },
  clearInterval: (id) => {
    globalThis.clearInterval(id);
  },
};
