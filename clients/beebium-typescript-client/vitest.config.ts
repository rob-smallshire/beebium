import { defineConfig } from "vitest/config";

export default defineConfig({
  test: {
    globals: true,
    testTimeout: 30000,
    hookTimeout: 30000,
    // Each integration test spawns a server process; limit parallelism
    // to avoid resource exhaustion on slow CI machines.
    fileParallelism: false,
  },
});
