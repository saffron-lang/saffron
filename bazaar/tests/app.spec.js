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
