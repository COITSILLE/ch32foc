export type Unsubscribe = () => void;

export function noop(): void {
  /* 空实现：用于"没有订阅者"的默认返回值 */
}

/** 极简发布/订阅容器：只支持同步通知，避免跨层引入异步时序问题。 */
export class Emitter<T> {
  private readonly listeners = new Set<(value: T) => void>();

  subscribe(cb: (value: T) => void): Unsubscribe {
    this.listeners.add(cb);
    return () => {
      this.listeners.delete(cb);
    };
  }

  emit(value: T): void {
    for (const cb of [...this.listeners]) {
      cb(value);
    }
  }

  get size(): number {
    return this.listeners.size;
  }

  clear(): void {
    this.listeners.clear();
  }
}
