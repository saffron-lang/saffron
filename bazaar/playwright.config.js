const { defineConfig } = require('@playwright/test');

module.exports = defineConfig({
    testDir: './tests',
    testMatch: '*.spec.js',
    timeout: 20000,
    retries: 1,
    use: {
        baseURL: 'http://localhost:8091',
        headless: true,
    },
    webServer: {
        command: 'node ../turmeric/tools/dev_server.js static --port 8091',
        port: 8091,
        timeout: 10000,
        reuseExistingServer: true,
    },
});
