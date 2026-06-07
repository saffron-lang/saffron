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

    // Track full navigation (form default submit causes a request to ?query)
    let fullReload = false;
    page.on('request', req => {
        // A native form submission would trigger a document request with form data
        if (req.isNavigationRequest() && req.url().includes('?')) {
            fullReload = true;
        }
    });

    // Submit form
    await input.press('Enter');
    await page.waitForTimeout(500);

    // Page should NOT have done a full form-submission navigation
    expect(fullReload).toBe(false);
    // URL should not have bare ? (native form submit artifact)
    expect(page.url()).not.toContain('?saffron');
});

test('search updates URL hash', async ({ page }) => {
    await page.goto('/');
    await page.waitForTimeout(1000);

    const input = page.locator('input[placeholder*="Search"]').first();
    await input.fill('test-pkg');
    await input.press('Enter');
    await page.waitForTimeout(1500);

    // Hash router should update to #/search?q=test-pkg
    const url = page.url();
    expect(url).toContain('#');
    expect(url).toContain('search');
    expect(url).toContain('q=test-pkg');
});

test('search triggers route change', async ({ page }) => {
    await page.goto('/');
    await page.waitForTimeout(1000);

    const input = page.locator('input[placeholder*="Search"]').first();
    await input.fill('hello');
    await input.press('Enter');
    await page.waitForTimeout(1500);

    // do_search() executes: navigates to search route via hash router
    const url = page.url();
    expect(url).toContain('#');
    expect(url).toContain('search');
    expect(url).toContain('q=hello');
});

test('search navigation renders search results page', async ({ page }) => {
    const logs = [];
    page.on('console', msg => logs.push(msg.text()));

    await page.goto('/');
    await page.waitForTimeout(1000);

    const input = page.locator('input[placeholder*="Search"]').first();
    await input.fill('mypackage');
    await input.press('Enter');
    await page.waitForTimeout(2000);

    // Debug: print console logs and innerHTML
    const innerHTML = await page.locator('#app').innerHTML();
    console.log('Console logs:', logs.slice(0, 20));
    console.log('innerHTML length:', innerHTML.length);
    console.log('innerHTML preview:', innerHTML.slice(0, 300));

    // Router should re-render: search results page replaces home page
    const text = await page.locator('#app').textContent();
    // SearchResultsPage shows "Results for" header with the query term
    expect(text).toContain('Results for');
    expect(text).toContain('mypackage');
    // Home page hero should no longer be visible
    expect(text).not.toContain('Find the right package');
});
