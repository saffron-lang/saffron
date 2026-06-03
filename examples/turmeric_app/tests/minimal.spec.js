const { test, expect } = require('@playwright/test');

test('shell div class reflects reactive_class', async ({ page }) => {
    await page.goto('/');
    // The first div in #app should have our test class
    const firstDiv = page.locator('#app > div');
    const cls = await firstDiv.getAttribute('class');
    console.log('First div class:', cls);
    // reactive_class should set the initial class based on theme signal
    expect(cls).toContain('theme-dark');
});

test('shell div updates after theme.set', async ({ page }) => {
    await page.goto('/');
    const firstDiv = page.locator('#app > div');
    const before = await firstDiv.getAttribute('class');
    console.log('Before toggle:', before);

    // Click toggle (sets theme to "light")
    await page.locator('.theme-toggle').click();
    await page.waitForTimeout(200);

    const after = await firstDiv.getAttribute('class');
    console.log('After toggle:', after);
    expect(after).toContain('light');
});
