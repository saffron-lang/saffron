const { test, expect } = require('@playwright/test');

test('debug login form submit', async ({ page }) => {
    const errors = [];
    page.on('pageerror', err => errors.push(err.message));

    await page.goto('/#/login');
    await page.waitForTimeout(2000);
    
    // Dispatch a submit event on the form
    const result = await page.evaluate(() => {
        const form = document.querySelector('.auth-form');
        if (!form) return 'no form';
        const event = new Event('submit', { bubbles: true, cancelable: true });
        form.dispatchEvent(event);
        return 'submitted';
    });
    console.log('SUBMIT RESULT:', result);
    
    await page.waitForTimeout(500);
    console.log('ERRORS:', JSON.stringify(errors));
    
    const text = await page.locator('#app').textContent();
    console.log('Contains "Please enter a username":', text.includes('Please enter a username'));
});
