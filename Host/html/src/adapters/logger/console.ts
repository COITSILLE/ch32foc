import type { Logger, LogLevel } from '../../ports/logger';

const ORDER: Record<LogLevel, number> = { debug: 10, info: 20, warn: 30, error: 40 };

export function createConsoleLogger(minLevel: LogLevel = 'info'): Logger {
  const enabled = (level: LogLevel): boolean => ORDER[level] >= ORDER[minLevel];
  return {
    debug: (message, ...rest) => {
      if (enabled('debug')) console.debug(`[foc] ${message}`, ...rest);
    },
    info: (message, ...rest) => {
      if (enabled('info')) console.info(`[foc] ${message}`, ...rest);
    },
    warn: (message, ...rest) => {
      if (enabled('warn')) console.warn(`[foc] ${message}`, ...rest);
    },
    error: (message, ...rest) => {
      if (enabled('error')) console.error(`[foc] ${message}`, ...rest);
    },
  };
}
