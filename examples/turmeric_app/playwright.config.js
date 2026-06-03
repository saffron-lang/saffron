const { defineConfig } = require('@playwright/test');

module.exports = defineConfig({
    testDir: './tests',
    timeout: 10000,
    use: {
        baseURL: 'http://localhost:8090',
        headless: true,
    },
    webServer: {
        command: 'node ../../turmeric/tools/dev_server.js build --port 8090',
        port: 8090,
        reuseExistingServer: true,
    },
});
