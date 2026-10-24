import { expect, test, type Page } from '@playwright/test';

/**
 * 只支持真机串口，所以 e2e 用一个**测试专用**的假 SerialPort（不是产品里的仿真模式）：
 * 它只做"收到命令 → 回一行合法应答"，用来验证 连接 → 轮询 → 使能 → 下发 → 断开 这条链路。
 */
const FAKE_SERIAL = `
(() => {
  const encoder = new TextEncoder();
  const decoder = new TextDecoder();
  window.__tx = [];

  const replyFor = (line) => {
    const parts = line.split(/\\s+/);
    const head = parts[0] || '';
    const arg = parts[1] || '';
    switch (head) {
      case 'PING': return 'PONG';
      case 'ADDR': return 'OK ADDR ' + arg;
      case 'FB': return 'FB 0.100 12.500 0.0000';
      case 'VBUS': return 'VBUS 24.000';
      case 'SYSREG': return 'SYSREG 0x00010000 1 0x00000000 0x00000000';
      case 'ENABLE': return 'OK ENABLE';
      case 'DISABLE': return 'OK DISABLE';
      case 'MODE': {
        const map = { CUR: '0', SPD: '1', POS: '2', CURRENT: '0', SPEED: '1', POSITION: '2' };
        const key = arg.toUpperCase();
        return 'OK MODE ' + (map[key] !== undefined ? map[key] : arg);
      }
      case 'SPD': return 'OK SPD ' + arg;
      case 'POS': return 'OK POS ' + arg;
      case 'IQ': return 'OK IQ ' + arg;
      case 'INFO': return 'INFO 0x00010000 0x00000413 0x00002204';
      default: return 'OK ' + head;
    }
  };

  let push = null;
  const readable = new ReadableStream({ start(controller) { push = controller; } });
  const writable = new WritableStream({
    write(chunk) {
      const line = decoder.decode(chunk).trim();
      window.__tx.push(line);
      setTimeout(() => {
        try { push.enqueue(encoder.encode(replyFor(line) + '\\r\\n')); } catch (e) { /* closed */ }
      }, 2);
    },
  });

  const port = {
    open: async () => {},
    close: async () => {},
    getInfo: () => ({ usbVendorId: 0x1a86, usbProductId: 0x7523 }),
    get readable() { return readable; },
    get writable() { return writable; },
  };

  Object.defineProperty(Navigator.prototype, 'serial', {
    configurable: true,
    get: () => ({
      requestPort: async () => port,
      getPorts: async () => [port],
      addEventListener: () => {},
      removeEventListener: () => {},
    }),
  });
})();
`;

async function openApp(page: Page): Promise<string[]> {
  const errors: string[] = [];
  page.on('pageerror', (error) => errors.push(error.message));
  page.on('console', (message) => {
    if (message.type() === 'error') errors.push(message.text());
  });
  await page.goto('/');
  return errors;
}

test('页面骨架：没有仿真入口，导航可切换', async ({ page }) => {
  const errors = await openApp(page);

  await expect(page.getByRole('heading', { level: 1 })).toHaveText('未连接');
  await expect(page.getByText('仿真总线')).toHaveCount(0);
  await expect(page.locator('.side select')).toHaveCount(0);
  await expect(page.locator('.side-foot')).toContainText('ASCII');
  await expect(page.locator('.stats')).toContainText('--');

  await page.getByRole('button', { name: '调试' }).click();
  await expect(page.locator('.debug-toolbar')).toContainText('未解锁');
  await page.getByRole('button', { name: '监控' }).click();
  await expect(page.locator('.stats')).toBeVisible();

  expect(errors).toEqual([]);
});

test('参数门禁：带宽/阻尼比不需要 DEBUG，过流/保存需要', async ({ page }) => {
  await page.addInitScript(FAKE_SERIAL);
  const errors = await openApp(page);

  await page.getByRole('button', { name: '连接' }).click();
  await expect(page.locator('.statusbar')).toContainText('1/1 在线', { timeout: 10_000 });

  await page.getByRole('button', { name: '调试' }).click();
  await expect(page.locator('.debug-toolbar')).toContainText('未解锁');

  const bwRow = page.locator('.bw-grid .row').first();
  await expect(bwRow.getByRole('button', { name: '写' })).toBeEnabled();
  await expect(page.getByRole('button', { name: '保存到 Flash' })).toBeDisabled();
  await expect(page.getByRole('button', { name: '写过流' })).toBeDisabled();

  await bwRow.getByRole('button', { name: '写' }).click();
  await expect(page.locator('.status-message')).toContainText('参数已写入 RAM');

  const sent = await page.evaluate(() => (window as unknown as { __tx: string[] }).__tx);
  expect(sent.some((line) => line.startsWith('BW CUR'))).toBe(true);

  expect(errors).toEqual([]);
});

test('两台设备可以同时跟踪，停一台不影响另一台', async ({ page }) => {
  await page.addInitScript(FAKE_SERIAL);
  const errors = await openApp(page);

  await page.getByRole('button', { name: '连接' }).click();
  await expect(page.locator('.statusbar')).toContainText('1/1 在线', { timeout: 10_000 });

  await page.locator('.side .num-input').first().fill('1');
  await page.getByRole('button', { name: '添加' }).click();
  await expect(page.locator('.device')).toHaveCount(2);

  // A 方波、B 正弦（各自按当前模式）
  await page.getByRole('button', { name: /电机 A/ }).click();
  await page.locator('.modebar').getByRole('button', { name: '速度' }).click();
  await page.getByRole('button', { name: /^方波/ }).click();

  await page.getByRole('button', { name: /电机 B/ }).click();
  await page.locator('.modebar').getByRole('button', { name: '速度' }).click();
  await page.getByRole('button', { name: /^正弦/ }).click();

  await expect(page.locator('.device').nth(0)).toContainText('跟踪中');
  await expect(page.locator('.device').nth(1)).toContainText('跟踪中');

  await page.waitForTimeout(600);
  const targets = await page.evaluate(() =>
    (window as unknown as { __tx: string[] }).__tx.filter((line) => line.startsWith('SPD')),
  );
  expect(targets.length).toBeGreaterThan(2);

  // 只停 A
  await page.getByRole('button', { name: /电机 A/ }).click();
  await page.getByRole('button', { name: '停止跟踪' }).click();
  await expect(page.locator('.device').nth(0)).not.toContainText('跟踪中');
  await expect(page.locator('.device').nth(1)).toContainText('跟踪中');

  expect(errors).toEqual([]);
});

test('位置环也能跑正弦，停止时稳在当前位置而不是回原点', async ({ page }) => {
  await page.addInitScript(FAKE_SERIAL);
  const errors = await openApp(page);
  const sent = (): Promise<string[]> =>
    page.evaluate(() => (window as unknown as { __tx: string[] }).__tx);

  await page.getByRole('button', { name: '连接' }).click();
  await expect(page.locator('.statusbar')).toContainText('1/1 在线', { timeout: 10_000 });

  // 切到位置环再开正弦：按钮文案应变成 rad
  await page.locator('.modebar').getByRole('button', { name: '位置' }).click();
  const sine = page.getByRole('button', { name: /^正弦/ });
  await expect(sine).toContainText('±0.5 rad');
  await expect(sine).not.toContainText('rad/s');

  await sine.click();
  await expect(page.locator('.device').first()).toContainText('跟踪中');
  await page.waitForTimeout(700);

  const posTargets = (await sent()).filter((line) => line.startsWith('POS'));
  expect(posTargets.length).toBeGreaterThan(1);
  // 幅度 0.5 rad，围绕开始时位置（假串口一直回 0）
  const values = posTargets.map((line) => Number(line.split(' ')[1]));
  expect(Math.max(...values)).toBeLessThanOrEqual(0.51);
  expect(Math.min(...values)).toBeGreaterThanOrEqual(-0.51);

  await page.getByRole('button', { name: '停止跟踪' }).click();
  await expect(page.locator('.device').first()).not.toContainText('跟踪中');
  const last = (await sent()).filter((line) => line.startsWith('POS')).at(-1);
  expect(last).toBe('POS 0');

  expect(errors).toEqual([]);
});

test('串口主流程：连接 → 轮询 → 使能 → 下发 → 断开', async ({ page }) => {
  await page.addInitScript(FAKE_SERIAL);
  const errors = await openApp(page);
  const sentLines = (): Promise<string[]> =>
    page.evaluate(() => (window as unknown as { __tx: string[] }).__tx);

  await page.getByRole('button', { name: '连接' }).click();
  await expect(page.locator('.statusbar')).toContainText('1/1 在线', { timeout: 10_000 });
  await expect(page.locator('.device')).toHaveCount(1);
  // 轮询真的在发 FB，且回包被解析进读数
  await expect(page.locator('.stats')).toContainText('12.5');
  await expect(page.locator('.stats')).toContainText('24.00');

  await page.getByRole('switch').click();
  await expect(page.locator('.head')).toContainText('已使能');

  await page.locator('.modebar').getByRole('button', { name: '速度' }).click();
  await expect(page.locator('.head')).toContainText('速度环');
  await page.locator('.target-row input').fill('30');
  await page.getByRole('button', { name: '下发' }).click();
  await expect(page.locator('.status-message')).toContainText('OK SPD 30');

  const sent = await sentLines();
  expect(sent).toContain('ADDR 0');
  expect(sent).toContain('PING');
  expect(sent).toContain('SPD 30');

  await page.getByRole('button', { name: '断开' }).click();
  await expect(page.locator('.statusbar')).toContainText('未连接');
  await expect(page.locator('.device')).toHaveCount(0);

  expect(errors).toEqual([]);
});
