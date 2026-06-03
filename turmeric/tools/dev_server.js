#!/usr/bin/env node
// Turmeric dev server with hot reload
// Usage: node tools/dev_server.js [build_dir] [--port 8080] [--watch src/]
//
// Serves static files from build_dir, injects a reload script into HTML,
// and watches for file changes to trigger browser reload via SSE.

const http = require('http');
const fs = require('fs');
const path = require('path');

const args = process.argv.slice(2);
let buildDir = 'build';
let port = 8080;
let watchDirs = [];

for (let i = 0; i < args.length; i++) {
    if (args[i] === '--port' && args[i + 1]) { port = parseInt(args[i + 1]); i++; }
    else if (args[i] === '--watch' && args[i + 1]) { watchDirs.push(args[i + 1]); i++; }
    else if (!args[i].startsWith('-')) { buildDir = args[i]; }
}

if (watchDirs.length === 0) watchDirs = [buildDir];

const MIME = {
    '.html': 'text/html', '.js': 'application/javascript', '.mjs': 'application/javascript',
    '.css': 'text/css', '.wasm': 'application/wasm', '.json': 'application/json',
    '.png': 'image/png', '.svg': 'image/svg+xml', '.ico': 'image/x-icon',
};

const RELOAD_SCRIPT = `
<script>
(function() {
    const es = new EventSource('/__reload');
    es.onmessage = function(e) {
        if (e.data === 'reload') window.location.reload();
    };
    es.onerror = function() {
        setTimeout(() => window.location.reload(), 1000);
    };
})();
</script>
`;

let sseClients = [];

function notifyReload() {
    for (const res of sseClients) {
        res.write('data: reload\n\n');
    }
}

const server = http.createServer((req, res) => {
    if (req.url === '/__reload') {
        res.writeHead(200, {
            'Content-Type': 'text/event-stream',
            'Cache-Control': 'no-cache',
            'Connection': 'keep-alive',
            'Access-Control-Allow-Origin': '*',
        });
        res.write('data: connected\n\n');
        sseClients.push(res);
        req.on('close', () => {
            sseClients = sseClients.filter(c => c !== res);
        });
        return;
    }

    let filePath = path.join(buildDir, req.url === '/' ? 'index.html' : req.url);
    if (!fs.existsSync(filePath)) {
        filePath = path.join(buildDir, 'index.html');
    }

    const ext = path.extname(filePath);
    const mime = MIME[ext] || 'application/octet-stream';

    try {
        let content = fs.readFileSync(filePath);
        if (ext === '.html') {
            content = content.toString().replace('</body>', RELOAD_SCRIPT + '</body>');
        }
        res.writeHead(200, {
            'Content-Type': mime,
            'Cache-Control': 'no-store, no-cache, must-revalidate',
        });
        res.end(content);
    } catch (e) {
        res.writeHead(404);
        res.end('Not found');
    }
});

let debounceTimer = null;
for (const dir of watchDirs) {
    if (!fs.existsSync(dir)) continue;
    fs.watch(dir, { recursive: true }, (eventType, filename) => {
        if (debounceTimer) clearTimeout(debounceTimer);
        debounceTimer = setTimeout(() => {
            console.log(`  [reload] ${filename || 'file'} changed`);
            notifyReload();
        }, 100);
    });
}

server.listen(port, () => {
    console.log(`\n  Turmeric dev server`);
    console.log(`  http://localhost:${port}`);
    console.log(`  Watching: ${watchDirs.join(', ')}`);
    console.log(`  Press Ctrl+C to stop\n`);
});
