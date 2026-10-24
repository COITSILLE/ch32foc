/** 开发期断言：只用于"程序写错了"的情况，业务失败请走 Result。 */
export function assert(condition: unknown, message: string): asserts condition {
  if (!condition) {
    throw new Error(`assertion failed: ${message}`);
  }
}

export function assertNever(value: never, message = 'unexpected value'): never {
  throw new Error(`${message}: ${String(value)}`);
}
