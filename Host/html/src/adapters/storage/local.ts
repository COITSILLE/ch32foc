import type { KeyValueStore } from '../../ports/storage';

/**
 * localStorage 实现：隐私模式或配额满时会抛异常，
 * 这里吞掉并退化成内存存储——偏好丢失不该让界面崩掉。
 */
export class LocalStore implements KeyValueStore {
  private readonly memory = new Map<string, string>();
  private available: boolean;

  constructor(private readonly prefix = 'foc-host:') {
    this.available = probe();
  }

  get(key: string): string | null {
    if (!this.available) return this.memory.get(key) ?? null;
    try {
      return localStorage.getItem(this.prefix + key);
    } catch {
      this.available = false;
      return this.memory.get(key) ?? null;
    }
  }

  set(key: string, value: string): void {
    this.memory.set(key, value);
    if (!this.available) return;
    try {
      localStorage.setItem(this.prefix + key, value);
    } catch {
      this.available = false;
    }
  }

  remove(key: string): void {
    this.memory.delete(key);
    if (!this.available) return;
    try {
      localStorage.removeItem(this.prefix + key);
    } catch {
      this.available = false;
    }
  }
}

function probe(): boolean {
  try {
    const key = '__foc_probe__';
    localStorage.setItem(key, '1');
    localStorage.removeItem(key);
    return true;
  } catch {
    return false;
  }
}
