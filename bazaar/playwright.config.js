const { defineConfig } = require('@playwright/test');

module.exports = defineConfig({
    testDir: './tests',
    timeout: 15000,
    use: {
        baseURL: 'http://localhost:8091',
        headless: true,
    },
    webServer: {
        command: 'node ../turmeric/tools/dev_server.js static --port 8091',
        port: 8091,
        reuseExistingServer: true,
    },
});
