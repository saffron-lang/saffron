const { test, expect } = require('@playwright/test');

test('reactive() text updates on signal change', async ({ page }) => {
    await page.goto('/');
    // The footer has reactive text showing theme value
    const footer = page.locator('.footer-stats');
    const initial = await footer.textContent();
    console.log('Initial footer:', initial);
    expect(initial).toContain('Theme: dark');

    // Click theme toggle
    await page.locator('.theme-toggle').click();
    await page.waitForTimeout(200);

    const after = await footer.textContent();
    console.log('After toggle:', after);
    // If reactive() works, this should now say "Theme: light"
    expect(after).toContain('Theme: light');
});

test('outlet re-renders but reactive_class does not update', async ({ page }) => {
    await page.goto('/');
    // Check if outlet renders (page content area)
    const content = await page.locator('.content').textContent();
    console.log('Content:', content);

    // Check shell class
    const shellClass = await page.locator('.app-shell').getAttribute('class');
    console.log('Shell class on load:', shellClass);
});
