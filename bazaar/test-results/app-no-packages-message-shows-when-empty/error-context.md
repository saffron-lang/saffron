# Instructions

- Following Playwright test failed.
- Explain why, be concise, respect Playwright best practices.
- Provide a snippet of code with the fix, if possible.

# Test info

- Name: app.spec.js >> no packages message shows when empty
- Location: tests/app.spec.js:40:1

# Error details

```
Error: expect(received).toContain(expected) // indexOf

Expected substring: "No packages"
Received string:    "BazaarDocsGitHubPublishLoginBazaar the Saffron package registryBuilt with Saffron + Turmeric"
```

# Page snapshot

```yaml
- generic [ref=e3]:
  - navigation [ref=e4]:
    - generic [ref=e5]:
      - generic [ref=e6] [cursor=pointer]: Bazaar
      - generic [ref=e7]:
        - link "Docs" [ref=e8] [cursor=pointer]:
          - /url: https://saffron-lang.org/docs
        - link "GitHub" [ref=e9] [cursor=pointer]:
          - /url: https://github.com/henry232323/saffron
        - generic [ref=e10] [cursor=pointer]: Publish
        - generic [ref=e11] [cursor=pointer]: Login
  - contentinfo [ref=e13]:
    - paragraph [ref=e14]: Bazaar the Saffron package registry
    - paragraph [ref=e15]: Built with Saffron + Turmeric
```

# Test source

```ts
  1  | const { test, expect } = require('@playwright/test');
  2  | 
  3  | test('app loads without wasm errors', async ({ page }) => {
  4  |     const errors = [];
  5  |     page.on('pageerror', err => errors.push(err.message));
  6  |     page.on('console', msg => {
  7  |         if (msg.type() === 'error') errors.push(msg.text());
  8  |     });
  9  | 
  10 |     await page.goto('/');
  11 |     await page.waitForTimeout(1000);
  12 | 
  13 |     if (errors.length > 0) {
  14 |         console.log('Page errors:', errors);
  15 |     }
  16 |     expect(errors.filter(e => e.includes('unreachable') || e.includes('RuntimeError'))).toHaveLength(0);
  17 | });
  18 | 
  19 | test('app renders content into #app', async ({ page }) => {
  20 |     await page.goto('/');
  21 |     await page.waitForTimeout(1000);
  22 |     const appContent = await page.locator('#app').innerHTML();
  23 |     expect(appContent.length).toBeGreaterThan(0);
  24 | });
  25 | 
  26 | test('navbar renders with Bazaar title', async ({ page }) => {
  27 |     await page.goto('/');
  28 |     await page.waitForTimeout(1000);
  29 |     const text = await page.locator('#app').textContent();
  30 |     expect(text).toContain('Bazaar');
  31 | });
  32 | 
  33 | test('search bar is present', async ({ page }) => {
  34 |     await page.goto('/');
  35 |     await page.waitForTimeout(1000);
  36 |     const placeholder = await page.locator('input[placeholder]').getAttribute('placeholder');
  37 |     expect(placeholder).toContain('Search');
  38 | });
  39 | 
  40 | test('no packages message shows when empty', async ({ page }) => {
  41 |     await page.goto('/');
  42 |     await page.waitForTimeout(1000);
  43 |     const text = await page.locator('#app').textContent();
> 44 |     expect(text).toContain('No packages');
     |                  ^ Error: expect(received).toContain(expected) // indexOf
  45 | });
  46 | 
```