/**
 * 时间来源注入：`core` 不直接读 `Date.now()`，测试才能用假时钟跑超时/重试。
 */
export interface Clock {
  now(): number;
  setTimeout(fn: () => void, ms: number): number;
  clearTimeout(id: number): void;
  setInterval(fn: () => void, ms: number): number;
  clearInterval(id: number): void;
}
