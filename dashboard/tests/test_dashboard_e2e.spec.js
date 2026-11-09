const { test, expect } = require('@playwright/test');

const DASHBOARD_URL = process.env.DASHBOARD_URL || 'http://127.0.0.1:8090';
const USER = process.env.DASHBOARD_USER || 'admin';
const PASSWORD = process.env.DASHBOARD_PASSWORD || 'admin';

test.beforeEach(async ({ page }) => {
  await page.goto(DASHBOARD_URL + '/login');
  await page.fill('#username', USER);
  await page.fill('#password', PASSWORD);
  await page.click('#login-btn');
  await expect(page.locator('.topbar')).toBeVisible();
});

test.describe('Dashboard End-to-End', () => {
  test('Dashboard displays main sections', async ({ page }) => {
    await expect(page.locator('.stat-cards')).toBeVisible();
    await expect(page.locator('.card.right-panel')).toBeVisible();
    await expect(page.locator('.pnl-card')).toBeVisible();
    await expect(page.locator('#candle-chart')).toBeVisible();
    await expect(page.locator('#pnl-chart')).toBeVisible();
    await expect(page.locator('#trade-log-body')).toBeAttached();
  });

  test('Start and stop simulation updates UI', async ({ page }) => {
    await page.click('#start-btn');
    await expect(page.locator('#start-btn')).toHaveText(/Running/);
    await page.click('#stop-btn');
    await expect(page.locator('#start-btn')).toHaveText(/Start Simulation/, { timeout: 10000 });
  });

  test('Clear session data empties the trade log', async ({ page }) => {
    page.on('dialog', dialog => dialog.accept());
    await page.click('#clear-btn');
    await expect.poll(() => page.locator('#trade-log-body tr').count(), { timeout: 6000 }).toBe(0);
  });

  test('Switching product updates the subtitle', async ({ page }) => {
    await page.selectOption('#product-select', { label: 'BTCUSDT' });
    await expect.poll(() => page.locator('.brand-text p').textContent(), { timeout: 6000 }).toContain('BTCUSDT');
  });
});
