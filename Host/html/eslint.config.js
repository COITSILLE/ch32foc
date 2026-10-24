import tseslint from 'typescript-eslint';

const browserGlobals = {
  window: 'readonly',
  document: 'readonly',
  navigator: 'readonly',
  localStorage: 'readonly',
  performance: 'readonly',
  requestAnimationFrame: 'readonly',
  cancelAnimationFrame: 'readonly',
  ResizeObserver: 'readonly',
  HTMLElement: 'readonly',
  HTMLCanvasElement: 'readonly',
  HTMLInputElement: 'readonly',
  TextDecoderStream: 'readonly',
  TextEncoder: 'readonly',
  TextDecoder: 'readonly',
  console: 'readonly',
  setTimeout: 'readonly',
  clearTimeout: 'readonly',
  setInterval: 'readonly',
  clearInterval: 'readonly',
  crypto: 'readonly',
};

export default tseslint.config(
  {
    ignores: ['dist/**', 'node_modules/**', 'test-results/**', 'playwright-report/**'],
  },
  ...tseslint.configs.recommended,
  {
    files: ['**/*.ts'],
    languageOptions: {
      globals: browserGlobals,
    },
    rules: {
      // TypeScript 已覆盖未定义标识符检查
      'no-undef': 'off',
      '@typescript-eslint/consistent-type-imports': ['error', { prefer: 'type-imports' }],
      '@typescript-eslint/no-unused-vars': ['error', { argsIgnorePattern: '^_' }],
      eqeqeq: ['error', 'always'],
      'prefer-const': 'error',
    },
  },
  {
    // core 是纯逻辑层：不许碰 DOM / 浏览器 API，也不许反向依赖上层
    files: ['src/core/**/*.ts'],
    rules: {
      'no-restricted-imports': [
        'error',
        {
          patterns: [
            { group: ['**/ui/*', '**/ui'], message: 'core 不能依赖 ui' },
            { group: ['**/app/*', '**/app'], message: 'core 不能依赖 app' },
            { group: ['**/adapters/*', '**/adapters'], message: 'core 不能依赖 adapters' },
          ],
        },
      ],
      'no-restricted-globals': [
        'error',
        { name: 'window', message: 'core 必须是纯逻辑，不能访问 DOM' },
        { name: 'document', message: 'core 必须是纯逻辑，不能访问 DOM' },
        { name: 'navigator', message: 'core 必须是纯逻辑，不能访问浏览器 API' },
        { name: 'localStorage', message: 'core 必须是纯逻辑，不能访问浏览器 API' },
      ],
    },
  },
  {
    // components 不得依赖 app 层
    files: ['src/ui/components/**/*.ts'],
    rules: {
      'no-restricted-imports': [
        'error',
        {
          patterns: [{ group: ['**/app/*', '**/app'], message: 'components 只接收数据，不读 store' }],
        },
      ],
    },
  },
);
