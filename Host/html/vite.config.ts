import { defineConfig } from 'vite';

export default defineConfig({
  // 相对路径：打包后可直接用静态服务器（或 Electron/Tauri）加载
  base: './',
  build: {
    target: 'es2022',
    outDir: 'dist',
    sourcemap: true,
  },
  server: {
    port: 5180,
  },
});
