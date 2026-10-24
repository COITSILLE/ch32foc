import { defineConfig } from 'vitest/config';

export default defineConfig({
  test: {
    // core 层测试不启动浏览器
    environment: 'node',
    include: ['tests/unit/**/*.test.ts'],
    restoreMocks: true,
  },
});
