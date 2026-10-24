/** 偏好、布局、最近连接等小对象；大对象（录制回放）另开 IndexedDB 实现。 */
export interface KeyValueStore {
  get(key: string): string | null;
  set(key: string, value: string): void;
  remove(key: string): void;
}

/** 内存实现：测试与"隐私模式禁用 localStorage"时兜底。 */
export class MemoryStore implements KeyValueStore {
  private readonly map = new Map<string, string>();

  get(key: string): string | null {
    return this.map.get(key) ?? null;
  }

  set(key: string, value: string): void {
    this.map.set(key, value);
  }

  remove(key: string): void {
    this.map.delete(key);
  }
}
