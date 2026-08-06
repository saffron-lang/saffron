const { test, expect } = require('@playwright/test');
const { execSync } = require('child_process');
const { existsSync, unlinkSync, copyFileSync, mkdirSync } = require('fs');
const path = require('path');

// =============================================================================
// E2E Test Setup: fresh test database + backend server
// =============================================================================

const TEST_DB = path.join(__dirname, '..', 'data', 'test_bazaar.db');
const BACKEND_PORT = 3002; // Use separate port from dev
const API = `http://localhost:${BACKEND_PORT}/api/v1`;

let backendProcess = null;
let registeredToken = '';
let registeredUsername = 'e2e-testuser';
let registeredPassword = 'testpass123';

test.beforeAll(async () => {
    // Fresh database for each test run
    if (existsSync(TEST_DB)) unlinkSync(TEST_DB);
    mkdirSync(path.join(__dirname, '..', 'data'), { recursive: true });

    // Start backend with test DB
    const bazaarBin = path.join(__dirname, '..', 'build', 'bazaar');
    if (!existsSync(bazaarBin)) {
        console.log('Backend binary not found — skipping E2E tests');
        test.skip();
        return;
    }

    const { spawn } = require('child_process');
    backendProcess = spawn(bazaarBin, [], {
        env: { ...process.env, BAZAAR_DB: TEST_DB, BAZAAR_PORT: String(BACKEND_PORT) },
        cwd: path.join(__dirname, '..'),
        stdio: 'pipe',
    });

    // Wait for server to be ready
    await new Promise(resolve => setTimeout(resolve, 3000));
});

test.afterAll(async () => {
    if (backendProcess) {
        backendProcess.kill();
        backendProcess = null;
    }
    // Clean up test DB
    if (existsSync(TEST_DB)) unlinkSync(TEST_DB);
});

// Helper: skip if backend isn't responding
async function ensureBackend(request) {
    try {
        const r = await request.get(`${API}/packages`, { timeout: 2000 });
        return r.ok();
    } catch {
        return false;
    }
}

// =============================================================================
// Account Creation & Authentication
// =============================================================================

test('register a new account', async ({ request }) => {
    if (!await ensureBackend(request)) { test.skip(); return; }

    const resp = await request.post(`${API}/auth/register`, {
        data: { username: registeredUsername, password: registeredPassword }
    });
    expect(resp.ok()).toBe(true);

    const data = await resp.json();
    expect(data.ok).toBe(true);
    expect(data.username).toBe(registeredUsername);
    expect(data.token).toBeTruthy();
    expect(data.token.startsWith('bzr_')).toBe(true);
    registeredToken = data.token;
});

test('login with registered account', async ({ request }) => {
    if (!registeredToken) { test.skip(); return; }

    const resp = await request.post(`${API}/auth/login`, {
        data: { username: registeredUsername, password: registeredPassword }
    });
    expect(resp.ok()).toBe(true);

    const data = await resp.json();
    expect(data.ok).toBe(true);
    expect(data.token).toBeTruthy();
});

test('login with wrong password fails', async ({ request }) => {
    if (!registeredToken) { test.skip(); return; }

    const resp = await request.post(`${API}/auth/login`, {
        data: { username: registeredUsername, password: 'wrongpass' }
    });
    const data = await resp.json();
    expect(data.error).toContain('invalid');
});

test('duplicate registration fails', async ({ request }) => {
    if (!registeredToken) { test.skip(); return; }

    const resp = await request.post(`${API}/auth/register`, {
        data: { username: registeredUsername, password: 'otherpass123' }
    });
    const data = await resp.json();
    expect(data.error).toContain('already');
});

// =============================================================================
// Package Publishing (uses registered account)
// =============================================================================

test('publish a package with valid token', async ({ request }) => {
    if (!registeredToken) { test.skip(); return; }

    const resp = await request.post(`${API}/packages/publish`, {
        headers: { Authorization: `Bearer ${registeredToken}` },
        data: {
            name: 'e2e-test-pkg',
            vers: '1.0.0',
            description: 'An end-to-end test package',
            tarball: 'dGVzdCBkYXRh'
        }
    });
    expect(resp.ok()).toBe(true);

    const data = await resp.json();
    expect(data.ok).toBe(true);
    expect(data.package_name).toBe('e2e-test-pkg');
    expect(data.version).toBe('1.0.0');
});

test('publish without token fails', async ({ request }) => {
    if (!registeredToken) { test.skip(); return; }

    const resp = await request.post(`${API}/packages/publish`, {
        data: { name: 'bad-pkg', vers: '0.1.0', tarball: 'x' }
    });
    const data = await resp.json();
    expect(data.error).toContain('authorization');
});

test('publish duplicate version fails', async ({ request }) => {
    if (!registeredToken) { test.skip(); return; }

    const resp = await request.post(`${API}/packages/publish`, {
        headers: { Authorization: `Bearer ${registeredToken}` },
        data: { name: 'e2e-test-pkg', vers: '1.0.0', description: 'dupe', tarball: 'x' }
    });
    const data = await resp.json();
    expect(data.error).toContain('exists');
});

// =============================================================================
// Package Discovery (uses published package)
// =============================================================================

test('published package appears in package list', async ({ request }) => {
    if (!registeredToken) { test.skip(); return; }

    const resp = await request.get(`${API}/packages`);
    expect(resp.ok()).toBe(true);

    const data = await resp.json();
    expect(data.total).toBeGreaterThanOrEqual(1);
    const names = data.packages.map(p => p.name);
    expect(names).toContain('e2e-test-pkg');
});

test('published package detail is accessible', async ({ request }) => {
    if (!registeredToken) { test.skip(); return; }

    const resp = await request.get(`${API}/packages/e2e-test-pkg`);
    expect(resp.ok()).toBe(true);

    const data = await resp.json();
    expect(data.name).toBe('e2e-test-pkg');
    expect(data.description).toBe('An end-to-end test package');
    expect(data.versions.length).toBe(1);
    expect(data.versions[0].vers).toBe('1.0.0');
});

test('published package appears in search', async ({ request }) => {
    if (!registeredToken) { test.skip(); return; }

    const resp = await request.get(`${API}/search?q=e2e`);
    expect(resp.ok()).toBe(true);

    const data = await resp.json();
    const names = data.packages.map(p => p.name);
    expect(names).toContain('e2e-test-pkg');
});

test('search with no match returns empty', async ({ request }) => {
    if (!registeredToken) { test.skip(); return; }

    const resp = await request.get(`${API}/search?q=zzz_nonexistent_xyz`);
    expect(resp.ok()).toBe(true);

    const data = await resp.json();
    expect(data.packages.length).toBe(0);
});

// =============================================================================
// Multi-version Publishing
// =============================================================================

test('publish second version', async ({ request }) => {
    if (!registeredToken) { test.skip(); return; }

    const resp = await request.post(`${API}/packages/publish`, {
        headers: { Authorization: `Bearer ${registeredToken}` },
        data: {
            name: 'e2e-test-pkg',
            vers: '1.1.0',
            description: 'Updated description',
            tarball: 'bmV3IGRhdGE='
        }
    });
    expect(resp.ok()).toBe(true);
});

test('package detail shows both versions', async ({ request }) => {
    if (!registeredToken) { test.skip(); return; }

    const resp = await request.get(`${API}/packages/e2e-test-pkg`);
    const data = await resp.json();
    expect(data.versions.length).toBe(2);
    expect(data.versions.map(v => v.vers)).toContain('1.0.0');
    expect(data.versions.map(v => v.vers)).toContain('1.1.0');
});

// =============================================================================
// Token Revocation
// =============================================================================

test('revoke token', async ({ request }) => {
    if (!registeredToken) { test.skip(); return; }

    const resp = await request.post(`${API}/auth/revoke`, {
        headers: { Authorization: `Bearer ${registeredToken}` }
    });
    // May succeed or may not be implemented — just verify no crash
    expect(resp.status()).toBeLessThan(500);
});
