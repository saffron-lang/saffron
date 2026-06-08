// Minimal static file server for Playwright tests
const http = require('http');
const fs = require('fs');
const path = require('path');

const MIME = {
    '.html': 'text/html',
    '.js': 'application/javascript',
    '.css': 'text/css',
    '.wasm': 'application/wasm',
    '.json': 'application/json',
};

const staticDir = path.join(__dirname, '..', 'static');

const server = http.createServer((req, res) => {
    let url = req.url.split('?')[0];
    if (url === '/') url = '/index.html';

    const filePath = path.join(staticDir, url);
    try {
        const data = fs.readFileSync(filePath);
        const ext = path.extname(filePath);
        res.writeHead(200, { 'Content-Type': MIME[ext] || 'application/octet-stream' });
        res.end(data);
    } catch {
        res.writeHead(404);
        res.end('not found');
    }
});

server.listen(8091, () => {
    console.log('Test server listening on http://localhost:8091');
});
