const { defineConfig } = require('@playwright/test');

module.exports = defineConfig({
  testDir: 'dashboard/tests',
  testMatch: '*.spec.js',
  workers: 1,
});
