import type { Unsubscribe } from '../shared/subscribe';

export type StateUpdater<S> = Partial<S> | ((state: S) => Partial<S>);

export interface Store<S> {
  getState(): S;
  /** 浅合并；返回新对象，快照永远不可变。 */
  setState(update: StateUpdater<S>): void;
  subscribe(cb: (state: S) => void): Unsubscribe;
}

/** 手写极简 store：不引框架，也不需要中间件。 */
export function createStore<S extends object>(initial: S): Store<S> {
  let state = initial;
  const listeners = new Set<(state: S) => void>();

  return {
    getState: () => state,
    setState(update) {
      const patch = typeof update === 'function' ? update(state) : update;
      state = { ...state, ...patch };
      for (const cb of [...listeners]) cb(state);
    },
    subscribe(cb) {
      listeners.add(cb);
      return () => {
        listeners.delete(cb);
      };
    },
  };
}
