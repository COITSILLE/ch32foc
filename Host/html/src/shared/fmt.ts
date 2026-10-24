/** 数字格式化：统一小数位，非有限值显示占位符。 */
export function fmtNum(value: number, decimals = 3): string {
  return Number.isFinite(value) ? value.toFixed(decimals) : '--';
}

/** 时间格式化：把毫秒差显示成 `m:ss` 或带单位的小时。 */
export function fmtDuration(ms: number): string {
  if (!Number.isFinite(ms) || ms < 0) return '--';
  const totalSec = Math.floor(ms / 1000);
  const h = Math.floor(totalSec / 3600);
  const m = Math.floor((totalSec % 3600) / 60);
  const s = totalSec % 60;
  if (h > 0) return `${h}h${String(m).padStart(2, '0')}m`;
  return `${m}:${String(s).padStart(2, '0')}`;
}

/** 时刻格式化：HH:MM:SS.mmm（日志时间戳用）。 */
export function fmtClock(epochMs: number): string {
  const d = new Date(epochMs);
  const pad = (v: number, n = 2): string => String(v).padStart(n, '0');
  return `${pad(d.getHours())}:${pad(d.getMinutes())}:${pad(d.getSeconds())}.${pad(d.getMilliseconds(), 3)}`;
}

export function fmtHex(value: number, digits = 8): string {
  if (!Number.isFinite(value)) return '--';
  return `0x${Math.trunc(value).toString(16).toUpperCase().padStart(digits, '0')}`;
}

/** 协议里的浮点：定点十进制，不用科学计数法（固件 parse_float 不认 e）。 */
export function fmtProtocolFloat(value: number, decimals = 4): string {
  if (!Number.isFinite(value)) return '0';
  const fixed = value.toFixed(decimals);
  const trimmed = fixed.includes('.') ? fixed.replace(/0+$/, '').replace(/\.$/, '') : fixed;
  return trimmed === '-0' ? '0' : trimmed;
}

export function clamp(value: number, min: number, max: number): number {
  return Math.min(max, Math.max(min, value));
}
