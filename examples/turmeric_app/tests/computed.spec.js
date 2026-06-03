const { test, expect } = require('@playwright/test');

test('computed doubled updates when counter changes', async ({ page }) => {
    await page.goto('/');
    await page.waitForTimeout(300);

    // Click +1 button (4th in button-row: -10, -1, Reset, +1, +10)
    await page.locator('.button-row button:text("+1")').first().click();
    await page.waitForTimeout(200);

    // Check computed values updated
    const content = await page.locator('.content').textContent();
    console.log('Content after +1:', content.slice(0, 200));

    // Doubled should be 2, Tripled should be 3
    expect(content).toContain('2');  // doubled
});
