const { test, expect } = require('@playwright/test');

test('app loads without wasm errors', async ({ page }) => {
    const errors = [];
    page.on('pageerror', err => errors.push(err.message));
    page.on('console', msg => {
        if (msg.type() === 'error') errors.push(msg.text());
    });

    await page.goto('/');
    await page.waitForTimeout(1000);

    if (errors.length > 0) {
        console.log('Page errors:', errors);
    }
    expect(errors.filter(e => e.includes('unreachable') || e.includes('RuntimeError'))).toHaveLength(0);
});

test('app renders content into #app', async ({ page }) => {
    await page.goto('/');
    await page.waitForTimeout(1000);
    const appContent = await page.locator('#app').innerHTML();
    expect(appContent.length).toBeGreaterThan(0);
});

test('navbar renders with Bazaar title', async ({ page }) => {
    await page.goto('/');
    await page.waitForTimeout(1000);
    const text = await page.locator('#app').textContent();
    expect(text).toContain('Bazaar');
});

test('search bar is present', async ({ page }) => {
    await page.goto('/');
    await page.waitForTimeout(1000);
    const placeholder = await page.locator('input[placeholder]').getAttribute('placeholder');
    expect(placeholder).toContain('Search');
});

test('no packages message shows when empty', async ({ page }) => {
    await page.goto('/');
    await page.waitForTimeout(1000);
    const text = await page.locator('#app').textContent();
    expect(text).toContain('No packages');
});

test('search form does not reload page', async ({ page }) => {
    await page.goto('/');
    await page.waitForTimeout(1000);

    // Type in search input
    const input = page.locator('input[placeholder*="Search"]').first();
    await input.fill('saffron');

    // Track navigation (page reload would change URL without hash)
    let navigated = false;
    page.on('framenavigated', () => { navigated = true; });

    // Submit form
    await input.press('Enter');
    await page.waitForTimeout(500);

    // Page should NOT have done a full navigation
    expect(navigated).toBe(false);
    // URL should not have bare ? (native form submit artifact)
    expect(page.url()).not.toContain('?saffron');
});

test.skip('search updates URL hash', async ({ page }) => {
    await page.goto('/');
    await page.waitForTimeout(1000);

    const input = page.locator('input[placeholder*="Search"]').first();
    await input.fill('test-pkg');
    await input.press('Enter');
    await page.waitForTimeout(1500);

    // Hash router should update to /search?q=...
    const url = page.url();
    expect(url).toContain('#');
});

test.skip('search triggers fetch request', async ({ page }) => {
    const requests = [];
    page.on('request', req => {
        if (req.url().includes('/api/') || req.url().includes('search')) {
            requests.push(req.url());
        }
    });

    await page.goto('/');
    await page.waitForTimeout(1000);

    const input = page.locator('input[placeholder*="Search"]').first();
    await input.fill('hello');
    await input.press('Enter');
    await page.waitForTimeout(1500);

    // Should have attempted a search (even if backend isn't running)
    expect(requests.length).toBeGreaterThan(0);
});
