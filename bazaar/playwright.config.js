const { defineConfig } = require('@playwright/test');

module.exports = defineConfig({
    testDir: './tests',
    timeout: 15000,
    projects: [
        {
            name: 'frontend',
            testMatch: 'app.spec.js',
            use: {
                baseURL: 'http://localhost:8091',
                headless: true,
            },
        },
        {
            name: 'api',
            testMatch: 'api.spec.js',
            use: {
                baseURL: 'http://localhost:3001',
                headless: true,
            },
        },
    ],
    webServer: {
        command: 'node ../turmeric/tools/dev_server.js static --port 8091',
        port: 8091,
        reuseExistingServer: true,
    },
});
