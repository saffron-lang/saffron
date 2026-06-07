// @ts-check
const { test, expect } = require('@playwright/test');

/**
 * Integration tests for the Bazaar registry API.
 *
 * These tests exercise the backend HTTP endpoints (parsley typed routes
 * over SQLite). They require the backend to be running on port 3001
 * (start with `pantry run dev` or `BAZAAR_PORT=3001 ./build/bazaar`).
 *
 * When the backend is unreachable the tests skip gracefully — CI can
 * omit the backend process and these will not fail.
 */

const API = 'http://localhost:3001/api/v1';

// Helper: skip the test when the backend is not reachable.
async function skipIfDown(request) {
    try {
        const r = await request.get(`${API}/packages`, { timeout: 2000 });
        return r;
    } catch {
        test.skip(true, 'backend not running');
        return null;
    }
}

// ---------------------------------------------------------------------------
// Package listing
// ---------------------------------------------------------------------------

test.describe('GET /api/v1/packages', () => {
    test('returns a package list with total', async ({ request }) => {
        const resp = await skipIfDown(request);
        if (!resp) return;

        expect(resp.status()).toBe(200);
        const data = await resp.json();
        expect(data).toHaveProperty('packages');
        expect(data).toHaveProperty('total');
        expect(Array.isArray(data.packages)).toBe(true);
        expect(typeof data.total).toBe('number');
    });

    test('supports offset/limit pagination', async ({ request }) => {
        const resp = await skipIfDown(request);
        if (!resp) return;

        const paginated = await request.get(`${API}/packages?offset=0&limit=5`);
        expect(paginated.status()).toBe(200);
        const data = await paginated.json();
        expect(data.packages.length).toBeLessThanOrEqual(5);
    });
});

// ---------------------------------------------------------------------------
// Package detail
// ---------------------------------------------------------------------------

test.describe('GET /api/v1/packages/:name', () => {
    test('returns 404 for a non-existent package', async ({ request }) => {
        const resp = await skipIfDown(request);
        if (!resp) return;

        const r = await request.get(`${API}/packages/this-package-does-not-exist-xyz`);
        expect(r.status()).toBe(404);
    });
});

// ---------------------------------------------------------------------------
// Search
// ---------------------------------------------------------------------------

test.describe('GET /api/v1/search', () => {
    test('returns results array with total', async ({ request }) => {
        const resp = await skipIfDown(request);
        if (!resp) return;

        const r = await request.get(`${API}/search?q=test`);
        expect(r.status()).toBe(200);
        const data = await r.json();
        expect(data).toHaveProperty('packages');
        expect(data).toHaveProperty('total');
        expect(Array.isArray(data.packages)).toBe(true);
    });

    test('returns all packages when query is empty', async ({ request }) => {
        const resp = await skipIfDown(request);
        if (!resp) return;

        const r = await request.get(`${API}/search?q=`);
        expect(r.status()).toBe(200);
        const data = await r.json();
        expect(Array.isArray(data.packages)).toBe(true);
    });

    test('respects limit parameter', async ({ request }) => {
        const resp = await skipIfDown(request);
        if (!resp) return;

        const r = await request.get(`${API}/search?q=&limit=2`);
        expect(r.status()).toBe(200);
        const data = await r.json();
        expect(data.packages.length).toBeLessThanOrEqual(2);
    });
});

// ---------------------------------------------------------------------------
// Auth — registration
// ---------------------------------------------------------------------------

test.describe('POST /api/v1/auth/register', () => {
    test('creates a new user and returns a token', async ({ request }) => {
        const resp = await skipIfDown(request);
        if (!resp) return;

        const username = `testuser_${Date.now()}`;
        const r = await request.post(`${API}/auth/register`, {
            data: { username },
        });
        expect(r.status()).toBe(200);
        const data = await r.json();
        expect(data.ok).toBe(true);
        expect(data.username).toBe(username);
        expect(data.token).toBeDefined();
        expect(data.token.startsWith('bzr_')).toBe(true);
    });

    test('rejects empty username', async ({ request }) => {
        const resp = await skipIfDown(request);
        if (!resp) return;

        const r = await request.post(`${API}/auth/register`, {
            data: { username: '' },
        });
        expect(r.status()).toBe(400);
    });

    test('rejects missing body', async ({ request }) => {
        const resp = await skipIfDown(request);
        if (!resp) return;

        const r = await request.post(`${API}/auth/register`, {
            data: {},
        });
        // Expect 400 — username is required
        expect(r.status()).toBe(400);
    });
});

// ---------------------------------------------------------------------------
// Auth — publish requires token
// ---------------------------------------------------------------------------

test.describe('POST /api/v1/packages/publish (auth)', () => {
    test('returns 401 without Authorization header', async ({ request }) => {
        const resp = await skipIfDown(request);
        if (!resp) return;

        const r = await request.post(`${API}/packages/publish`, {
            data: { name: 'x', vers: '0.0.1', tarball: 'dGVzdA==' },
        });
        expect(r.status()).toBe(401);
    });

    test('returns 401 with an invalid token', async ({ request }) => {
        const resp = await skipIfDown(request);
        if (!resp) return;

        const r = await request.post(`${API}/packages/publish`, {
            headers: { Authorization: 'Bearer invalid_token_abc' },
            data: { name: 'x', vers: '0.0.1', tarball: 'dGVzdA==' },
        });
        expect(r.status()).toBe(401);
    });
});

// ---------------------------------------------------------------------------
// Full publish flow: register -> publish -> get -> search -> yank
// ---------------------------------------------------------------------------

test.describe('full publish lifecycle', () => {
    let token = '';
    const pkgName = `test-pkg-${Date.now()}`;
    const version = '1.0.0';

    test.beforeAll(async ({ request }) => {
        try {
            await request.get(`${API}/packages`, { timeout: 2000 });
        } catch {
            return; // backend down — individual tests will skip
        }
        const r = await request.post(`${API}/auth/register`, {
            data: { username: `lifecycle_${Date.now()}` },
        });
        if (r.ok()) {
            const data = await r.json();
            token = data.token;
        }
    });

    test('publish a new package version', async ({ request }) => {
        const resp = await skipIfDown(request);
        if (!resp) return;
        if (!token) { test.skip(true, 'no auth token'); return; }

        const r = await request.post(`${API}/packages/publish`, {
            headers: { Authorization: `Bearer ${token}` },
            data: {
                name: pkgName,
                vers: version,
                description: 'Integration test package',
                repository: 'https://github.com/test/test',
                license: 'MIT',
                tarball: 'dGVzdA==', // base64("test")
            },
        });
        expect(r.status()).toBe(200);
        const data = await r.json();
        expect(data.ok).toBe(true);
        expect(data.package_name).toBe(pkgName);
        expect(data.version).toBe(version);
    });

    test('duplicate publish returns 409 conflict', async ({ request }) => {
        const resp = await skipIfDown(request);
        if (!resp) return;
        if (!token) { test.skip(true, 'no auth token'); return; }

        const r = await request.post(`${API}/packages/publish`, {
            headers: { Authorization: `Bearer ${token}` },
            data: {
                name: pkgName,
                vers: version,
                tarball: 'dGVzdA==',
            },
        });
        expect(r.status()).toBe(409);
    });

    test('get the published package', async ({ request }) => {
        const resp = await skipIfDown(request);
        if (!resp) return;
        if (!token) { test.skip(true, 'no auth token'); return; }

        const r = await request.get(`${API}/packages/${pkgName}`);
        expect(r.status()).toBe(200);
        const data = await r.json();
        expect(data.name).toBe(pkgName);
        expect(data.description).toBe('Integration test package');
        expect(data.repository).toBe('https://github.com/test/test');
        expect(data.license).toBe('MIT');
        expect(data.versions).toHaveLength(1);
        expect(data.versions[0].vers).toBe(version);
        expect(data.versions[0].yanked).toBe(false);
    });

    test('package appears in listing', async ({ request }) => {
        const resp = await skipIfDown(request);
        if (!resp) return;
        if (!token) { test.skip(true, 'no auth token'); return; }

        const r = await request.get(`${API}/packages`);
        expect(r.status()).toBe(200);
        const data = await r.json();
        const found = data.packages.find(p => p.name === pkgName);
        expect(found).toBeDefined();
        expect(found.latest).toBe(version);
    });

    test('package is searchable', async ({ request }) => {
        const resp = await skipIfDown(request);
        if (!resp) return;
        if (!token) { test.skip(true, 'no auth token'); return; }

        const r = await request.get(`${API}/search?q=${pkgName}`);
        expect(r.status()).toBe(200);
        const data = await r.json();
        const found = data.packages.find(p => p.name === pkgName);
        expect(found).toBeDefined();
    });

    test('yank the version', async ({ request }) => {
        const resp = await skipIfDown(request);
        if (!resp) return;
        if (!token) { test.skip(true, 'no auth token'); return; }

        const r = await request.post(`${API}/packages/${pkgName}/yank/${version}`, {
            headers: { Authorization: `Bearer ${token}` },
        });
        expect(r.status()).toBe(200);
        const data = await r.json();
        expect(data.ok).toBe(true);
        expect(data.yanked).toBe(true);
    });

    test('yanked version is reflected in package detail', async ({ request }) => {
        const resp = await skipIfDown(request);
        if (!resp) return;
        if (!token) { test.skip(true, 'no auth token'); return; }

        const r = await request.get(`${API}/packages/${pkgName}`);
        expect(r.status()).toBe(200);
        const data = await r.json();
        expect(data.versions[0].yanked).toBe(true);
    });
});

// ---------------------------------------------------------------------------
// Publish validation
// ---------------------------------------------------------------------------

test.describe('POST /api/v1/packages/publish (validation)', () => {
    let token = '';

    test.beforeAll(async ({ request }) => {
        try {
            await request.get(`${API}/packages`, { timeout: 2000 });
        } catch {
            return;
        }
        const r = await request.post(`${API}/auth/register`, {
            data: { username: `val_${Date.now()}` },
        });
        if (r.ok()) {
            token = (await r.json()).token;
        }
    });

    test('rejects missing required fields', async ({ request }) => {
        const resp = await skipIfDown(request);
        if (!resp) return;
        if (!token) { test.skip(true, 'no auth token'); return; }

        const r = await request.post(`${API}/packages/publish`, {
            headers: { Authorization: `Bearer ${token}` },
            data: { name: 'some-pkg' }, // missing vers and tarball
        });
        expect(r.status()).toBe(400);
    });

    test('rejects empty body', async ({ request }) => {
        const resp = await skipIfDown(request);
        if (!resp) return;
        if (!token) { test.skip(true, 'no auth token'); return; }

        const r = await request.post(`${API}/packages/publish`, {
            headers: { Authorization: `Bearer ${token}` },
            data: {},
        });
        expect(r.status()).toBe(400);
    });
});

// ---------------------------------------------------------------------------
// Yank authorization
// ---------------------------------------------------------------------------

test.describe('POST /api/v1/packages/:name/yank/:version (auth)', () => {
    test('returns 401 without token', async ({ request }) => {
        const resp = await skipIfDown(request);
        if (!resp) return;

        const r = await request.post(`${API}/packages/some-pkg/yank/1.0.0`);
        expect(r.status()).toBe(401);
    });

    test('returns 404 for non-existent package', async ({ request }) => {
        const resp = await skipIfDown(request);
        if (!resp) return;

        // Register to get a valid token
        const regResp = await request.post(`${API}/auth/register`, {
            data: { username: `yank_test_${Date.now()}` },
        });
        if (!regResp.ok()) { test.skip(true, 'registration failed'); return; }
        const { token } = await regResp.json();

        const r = await request.post(`${API}/packages/nonexistent-pkg-xyz/yank/0.0.1`, {
            headers: { Authorization: `Bearer ${token}` },
        });
        expect(r.status()).toBe(404);
    });
});

// ---------------------------------------------------------------------------
// Token revocation
// ---------------------------------------------------------------------------

test.describe('POST /api/v1/auth/revoke', () => {
    test('revokes the token so subsequent calls fail', async ({ request }) => {
        const resp = await skipIfDown(request);
        if (!resp) return;

        // Register
        const regResp = await request.post(`${API}/auth/register`, {
            data: { username: `revoke_${Date.now()}` },
        });
        expect(regResp.status()).toBe(200);
        const { token } = await regResp.json();

        // Revoke
        const revokeResp = await request.post(`${API}/auth/revoke`, {
            headers: { Authorization: `Bearer ${token}` },
        });
        expect(revokeResp.status()).toBe(200);
        const revokeData = await revokeResp.json();
        expect(revokeData.ok).toBe(true);
        expect(revokeData.revoked).toBe(true);

        // Token should now be invalid
        const publishResp = await request.post(`${API}/packages/publish`, {
            headers: { Authorization: `Bearer ${token}` },
            data: { name: 'x', vers: '0.0.1', tarball: 'dGVzdA==' },
        });
        expect(publishResp.status()).toBe(401);
    });

    test('returns 401 without token', async ({ request }) => {
        const resp = await skipIfDown(request);
        if (!resp) return;

        const r = await request.post(`${API}/auth/revoke`);
        expect(r.status()).toBe(401);
    });
});

// ---------------------------------------------------------------------------
// Download endpoint
// ---------------------------------------------------------------------------

test.describe('GET /api/v1/packages/:name/:version/download', () => {
    test('returns 404 for non-existent version', async ({ request }) => {
        const resp = await skipIfDown(request);
        if (!resp) return;

        const r = await request.get(`${API}/packages/nonexistent-pkg/0.0.1/download`);
        expect(r.status()).toBe(404);
    });

    test('downloads a published package tarball', async ({ request }) => {
        const resp = await skipIfDown(request);
        if (!resp) return;

        // Register and publish
        const regResp = await request.post(`${API}/auth/register`, {
            data: { username: `dl_${Date.now()}` },
        });
        if (!regResp.ok()) { test.skip(true, 'registration failed'); return; }
        const { token } = await regResp.json();

        const pkgName = `dl-test-${Date.now()}`;
        const pubResp = await request.post(`${API}/packages/publish`, {
            headers: { Authorization: `Bearer ${token}` },
            data: {
                name: pkgName,
                vers: '0.1.0',
                description: 'download test',
                tarball: 'dGVzdCBjb250ZW50', // base64("test content")
            },
        });
        if (!pubResp.ok()) { test.skip(true, 'publish failed'); return; }

        // Download
        const dlResp = await request.get(`${API}/packages/${pkgName}/0.1.0/download`);
        expect(dlResp.status()).toBe(200);
        expect(dlResp.headers()['content-type']).toContain('application/gzip');
        expect(dlResp.headers()['content-disposition']).toContain(`${pkgName}-0.1.0.tar.gz`);
    });
});
