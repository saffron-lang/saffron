const { test, expect } = require('@playwright/test');

test('app loads without crashing', async ({ page }) => {
    await page.goto('/');
    const body = await page.locator('body').textContent();
    expect(body.length).toBeGreaterThan(0);
});

test('outlet renders page content on load', async ({ page }) => {
    await page.goto('/');
    const content = await page.locator('.content').textContent();
    expect(content).not.toContain('404');
    expect(content.length).toBeGreaterThan(10);
});

test('outlet renders counter page by default', async ({ page }) => {
    await page.goto('/');
    const content = await page.locator('.content').textContent();
    expect(content).toContain('Counter');
});

test('navigation changes footer and re-renders outlet', async ({ page }) => {
    await page.goto('/');
    await page.locator('button:text("Todos")').click();
    await page.waitForTimeout(500);
    const footer = await page.locator('.footer-stats').textContent();
    console.log('Footer:', footer);
    expect(footer).toContain('/todos');
    const content = await page.locator('.content').textContent();
    console.log('Content:', content.slice(0, 80));
    expect(content).toContain('Todo');
});

test('dark mode toggle changes shell class', async ({ page }) => {
    await page.goto('/');
    const shell = page.locator('#app > div');
    await expect(shell).toHaveClass(/theme-dark/);
    await page.locator('.theme-toggle').click();
    await page.waitForTimeout(200);
    await expect(shell).toHaveClass(/theme-light/);
});

test('nav buttons get active class on route change', async ({ page }) => {
    await page.goto('/');
    await page.waitForTimeout(200);
    const counterBtn = page.locator('button:text("Counter")');
    await expect(counterBtn).toHaveClass(/active/);
    await page.locator('button:text("Todos")').click();
    await page.waitForTimeout(200);
    const todosBtn = page.locator('button:text("Todos")');
    await expect(todosBtn).toHaveClass(/active/);
    const counterCls = await counterBtn.getAttribute('class');
    expect(counterCls).not.toContain('active');
});

test('URL hash updates on navigation', async ({ page }) => {
    await page.goto('/');
    await page.locator('button:text("Timer")').click();
    await page.waitForTimeout(200);
    const url = page.url();
    expect(url).toContain('#');
    expect(url).toContain('timer');
});
