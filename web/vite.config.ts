import { defineConfig } from 'vitest/config'
import react from '@vitejs/plugin-react'

export default defineConfig({
  base: '/',
  plugins: [react()],
  build: {
    outDir: 'dist',
    emptyOutDir: true,
    target: 'safari15',
    cssCodeSplit: false,
  },
  test: { environment: 'node', include: ['src/**/*.test.ts'] },
})
