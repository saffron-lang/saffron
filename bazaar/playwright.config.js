const { defineConfig } = require('@playwright/test');

module.exports = defineConfig({
    testDir: './tests',
    testMatch: '*.spec.js',
    timeout: 20000,
    retries: 2,
    use: {
        baseURL: 'http://localhost:8091',
        headless: true,
    },
    webServer: {
        command: 'node tests/server.js',
        port: 8091,
        timeout: 15000,
        reuseExistingServer: true,
    },
});
