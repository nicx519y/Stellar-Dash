import { expect, test, type Page } from '@playwright/test';

const catalogKey = 'xora-preview-firmware-releases-v1';

async function openFirmware(page: Page) {
  await page.addInitScript(() => localStorage.setItem('preferred_language', 'en'));
  await page.goto('/firmware/');
  await expect(page.getByRole('heading', { name: 'XORA 2.0.0' })).toBeVisible();
}

test('release details use a full-width native disclosure row', async ({ page }) => {
  await openFirmware(page);
  const trigger = page.locator('summary').filter({ hasText: 'View details' }).nth(1);
  await trigger.scrollIntoViewIfNeeded();
  const row = await trigger.boundingBox();
  expect(row).not.toBeNull();
  await page.mouse.click(row!.x + row!.width - 8, row!.y + row!.height / 2);
  await expect(trigger.locator('..')).toHaveAttribute('open', '');
  await expect(trigger.locator('..').getByText('Included components')).toBeVisible();
  await expect(page.getByRole('dialog')).toHaveCount(0);
  await trigger.click();
  await expect(trigger.locator('..').getByText('Included components')).toBeHidden();
  await expect(trigger.locator('..')).not.toHaveAttribute('open', '');
});

test('an incompatible release shows its blocking reason, and repeated install clicks open one confirmation', async ({ page }) => {
  await openFirmware(page);
  await expect(page.getByText('Configuration format is incompatible')).toBeVisible();
  await expect(page.getByRole('button', { name: 'Install release' }).first()).toBeDisabled();

  const install = page.getByRole('button', { name: 'Install release' }).nth(1);
  await install.evaluate(button => { (button as HTMLButtonElement).click(); (button as HTMLButtonElement).click(); });
  const dialog = page.getByRole('dialog');
  await expect(dialog).toHaveCount(1);
  await expect(dialog.getByRole('heading', { name: 'Install XORA 2.0.0' })).toBeVisible();
  await expect(dialog.getByRole('button', { name: 'Confirm installation' })).toBeVisible();
  await expect(dialog.getByRole('progressbar')).toHaveCount(0);
  await dialog.getByRole('button', { name: 'Cancel' }).click();
  await expect(dialog).toBeHidden();
});

test('confirmation starts installation without a physical authorization step', async ({ page }) => {
  await openFirmware(page);
  await page.evaluate(() => {
    const values: number[] = [];
    (window as typeof window & { installProgressValues: number[] }).installProgressValues = values;
    const steps: number[] = [0];
    (window as typeof window & { installDisplaySteps: number[] }).installDisplaySteps = steps;
    new MutationObserver(() => {
      const ring = document.querySelector('[role="progressbar"][aria-label="Overall installation progress"]');
      if (ring) values.push(Number(ring.getAttribute('aria-valuenow')));
      const timeline = document.querySelector('[data-testid="install-step-timeline"]');
      if (timeline) steps.push(Number(timeline.getAttribute('data-step')));
    }).observe(document.body, { subtree: true, childList: true, attributes: true, attributeFilter: ['aria-valuenow', 'data-step'] });
  });
  await page.getByRole('button', { name: 'Install release' }).nth(1).click();
  const dialog = page.getByRole('dialog');
  await dialog.getByRole('button', { name: 'Confirm installation' }).click();
  await expect(dialog.getByRole('button', { name: 'Authorized, continue' })).toHaveCount(0);
  await expect(dialog).toBeVisible();
  await expect.poll(() => page.evaluate(() => Boolean(localStorage.getItem('xora-release-install-v2')))).toBe(true);
  const activeTask = await page.evaluate(() => JSON.parse(localStorage.getItem('xora-release-install-v2')!));
  await expect.poll(async () => page.evaluate(() =>
    JSON.parse(sessionStorage.getItem('xora-mock-install') || '{}').phase)).toBe('completed');
  await expect(dialog.getByRole('status')).toHaveText('Update complete');
  await expect(dialog.getByTestId('tx-install-mode')).toHaveCount(0);
  await expect(dialog.getByTestId('tx-recovery-mode')).toHaveCount(0);
  await expect(dialog.getByRole('progressbar')).toHaveAttribute('aria-valuenow', '100');
  await expect(dialog.getByTestId('install-step-timeline')).toHaveAttribute('data-step', '2');
  expect(await dialog.getByTestId('install-step-timeline').locator(':scope > div > div').evaluateAll(nodes =>
    nodes.every(node => getComputedStyle(node).alignItems === 'flex-start'))).toBe(true);
  await expect(dialog.getByText('Install update', { exact: true })).toBeVisible();
  await expect(dialog.getByText('Write inactive controller slot', { exact: true })).toHaveCount(0);
  const ring = dialog.getByRole('progressbar');
  expect((await ring.boundingBox())!.width).toBe(240);
  await expect(ring.locator('circle').last()).toHaveAttribute('stroke-width', '5');
  await expect(ring.locator('circle').last()).toHaveAttribute('stroke', /^url\(#install-gradient-/);
  expect((await dialog.boundingBox())!.height).toBeLessThanOrEqual(page.viewportSize()!.height - 24);
  expect(await dialog.evaluate(node => [...node.querySelectorAll('*')].every(child =>
    child.scrollHeight <= child.clientHeight + 1 || !['auto', 'scroll'].includes(getComputedStyle(child).overflowY)))).toBe(true);
  const steps = await page.evaluate(() => (window as typeof window & { installDisplaySteps: number[] }).installDisplaySteps);
  expect(new Set(steps)).toEqual(new Set([0, 1, 2]));
  expect(steps).toEqual([...steps].sort((a, b) => a - b));
  await dialog.screenshot({ path: 'output/playwright/firmware-install-completed-en.png' });
  const values = await page.evaluate(() => (window as typeof window & { installProgressValues: number[] }).installProgressValues);
  expect(values.length).toBeGreaterThan(3);
  expect(values).toEqual([...values].sort((a, b) => a - b));
  expect(values).toContain(80);
  await dialog.getByRole('button', { name: 'Close' }).click();
  await expect(page.getByText('Verified', { exact: true })).toBeVisible();
  await page.getByRole('tab', { name: 'Global Setting', exact: true }).click();
  await expect(page.getByRole('tab', { name: 'Global Setting', exact: true })).toHaveAttribute('aria-selected', 'true');
  await page.getByRole('tab', { name: 'Firmware', exact: true }).click();
  await expect(page.getByRole('dialog')).toHaveCount(0);
  await expect(page.getByText('Verified', { exact: true })).toBeVisible();
  await page.getByTestId('config-sync-finish-button').click();
  await page.getByRole('dialog').getByRole('button', { name: 'Confirm', exact: true }).click();
  await expect(page.getByTestId('device-status-card')).toHaveAttribute('data-connection-state', 'waiting');
  await expect(page.getByRole('dialog', { name: 'Device Not Connected' })).toBeVisible();
  await expect(page.getByText('Device disconnected. Check power, USB and WebConfig mode.', { exact: true })).toHaveCount(0);
  expect(await page.evaluate(() => localStorage.getItem('xora-release-install-v2'))).toBeNull();
  await page.getByRole('dialog', { name: 'Device Not Connected' }).getByRole('button', { name: 'Reconnect Device', exact: true }).click();
  await expect(page.getByTestId('device-status-card')).toBeHidden();
  // Compatibility: a completed record written by an older WebConfig must not reopen its dialog.
  await page.evaluate(task => localStorage.setItem('xora-release-install-v2', JSON.stringify({
    ...task, result: 'completed', progress: { stage: 'completed', overallPercent: 100, stepIndex: 9 },
  })), activeTask);
  await page.reload();
  await expect(page.getByRole('heading', { name: 'XORA 2.0.0' })).toBeVisible();
  await expect(page.getByTestId('device-status-card')).toBeHidden();
  await expect(page.getByRole('dialog')).toHaveCount(0);
  await expect(page.getByRole('progressbar')).toHaveCount(0);
  expect(await page.evaluate(() => localStorage.getItem('xora-release-install-v2'))).toBeNull();
});

test('simplified timeline fits the narrow Chinese progress dialog', async ({ page }) => {
  await page.setViewportSize({ width: 390, height: 740 });
  await page.emulateMedia({ reducedMotion: 'reduce' });
  await page.addInitScript(() => localStorage.setItem('preferred_language', 'zh'));
  await page.goto('/firmware/');
  await expect(page.getByRole('heading', { name: 'XORA 2.0.0' })).toBeVisible();
  await page.getByRole('button', { name: '安装此版本', exact: true }).nth(1).click();
  const dialog = page.getByRole('dialog');
  await dialog.getByRole('button', { name: '确认安装' }).click();
  await expect(dialog.getByTestId('install-step-timeline')).toHaveAttribute('data-step', '1');
  await dialog.screenshot({ path: 'output/playwright/firmware-install-backup-zh-mobile.png', animations: 'disabled' });
  await expect(dialog.getByRole('progressbar')).toHaveAttribute('aria-valuenow', '100');
  const timeline = dialog.getByTestId('install-step-timeline');
  await expect(timeline).toHaveAttribute('data-step', '2');
  await expect(timeline.getByText('安装升级', { exact: true })).toBeVisible();
  await expect(dialog.getByText('写入主控备用槽', { exact: true })).toHaveCount(0);
  const bounds = (await dialog.boundingBox())!;
  expect(bounds.x).toBeGreaterThanOrEqual(0);
  expect(bounds.x + bounds.width).toBeLessThanOrEqual(390);
  expect(bounds.height).toBeLessThanOrEqual(490);
  expect(await page.evaluate(() => document.documentElement.scrollWidth)).toBe(390);
  expect(await timeline.evaluate(element => getComputedStyle(element).maskImage)).toContain('linear-gradient');
  expect(await timeline.locator(':scope > div').evaluate(element => getComputedStyle(element).transitionDuration)).toBe('0s');
  await expect(dialog.getByRole('button', { name: '关闭', exact: true })).toBeInViewport();
  await dialog.screenshot({ path: 'output/playwright/firmware-install-completed-zh-mobile.png' });
});

test('old firmware physical gate is reported without offering an unusable retry', async ({ page }) => {
  await openFirmware(page);
  await page.evaluate(() => sessionStorage.setItem('xora-mock-legacy-confirmation-reject', '1'));
  await page.getByRole('button', { name: 'Install release' }).nth(1).click();
  const dialog = page.getByRole('dialog');
  await dialog.getByRole('button', { name: 'Confirm installation' }).click();
  await expect(dialog.getByRole('alert')).toContainText(/baseline does not support TX readback backups/i);
  await expect(dialog.getByRole('button', { name: 'Retry installation' })).toHaveCount(0);
  await expect(page.getByRole('heading', { name: 'Result uncertain; reconnect to read device status' })).toHaveCount(0);
  const phase = await page.evaluate(() => JSON.parse(sessionStorage.getItem('xora-mock-install') || '{"phase":"idle"}').phase);
  expect(phase).toBe('idle');
  await dialog.getByRole('button', { name: 'Close' }).click();
  await expect(dialog).toBeHidden();
});

test('long notes stay compact in the list, remain complete in details, and empty results are clear', async ({ page }) => {
  await openFirmware(page);
  const longNotes = 'XORA release notes with complete details. '.repeat(18);
  await page.evaluate(({ key, notes }) => {
    const items = JSON.parse(sessionStorage.getItem(key) || '[]');
    const release = items.find((item: { id: string }) => item.id === 'preview-2.0.0');
    release.notes = notes;
    sessionStorage.setItem(key, JSON.stringify(items));
  }, { key: catalogKey, notes: longNotes });
  await page.reload();
  await expect(page.getByRole('heading', { name: 'XORA 2.0.0' })).toBeVisible();
  const summary = page.getByText(longNotes).first();
  expect(await summary.evaluate(element => getComputedStyle(element).webkitLineClamp)).toBe('2');
  const trigger = page.locator('summary').filter({ hasText: 'View details' }).nth(1);
  await trigger.click();
  await expect(page.getByText(longNotes).last()).toBeVisible();
  expect(await summary.evaluate(element => getComputedStyle(element).webkitLineClamp)).toBe('2');
  await trigger.click();
  await page.getByRole('textbox', { name: 'Search firmware' }).fill('2.0.0');
  await expect(page.getByText('1 release', { exact: true })).toBeVisible();
  await expect(page.getByRole('button', { name: 'Previous' })).toHaveCount(0);
  await page.getByRole('textbox', { name: 'Search firmware' }).fill('no-such-release');
  await expect(page.getByText('No matching published firmware.')).toBeVisible();
  await expect(page.getByText('0 releases')).toBeVisible();
  await page.evaluate(key => sessionStorage.setItem(key, '{invalid'), catalogKey);
  await page.getByRole('button', { name: 'Refresh releases' }).click();
  await expect(page.getByRole('alert').last()).toBeVisible();
});

test('multiple catalog pages keep the count visible and put page controls in one place', async ({ page }) => {
  await openFirmware(page);
  await page.evaluate(key => {
    const items = JSON.parse(sessionStorage.getItem(key) || '[]');
    const template = items.find((item: { id: string }) => item.id === 'preview-2.0.0');
    const extras = Array.from({ length: 19 }, (_, index) => ({
      ...template,
      id: `preview-extra-${index}`,
      manifest: { ...template.manifest, version: `3.0.${index}` },
    }));
    sessionStorage.setItem(key, JSON.stringify([...items, ...extras]));
  }, catalogKey);
  await page.reload();
  await expect(page.getByText('22 releases')).toBeVisible();
  await expect(page.getByText('Page 1 of 2')).toBeVisible();
  await page.getByRole('button', { name: 'Next' }).click();
  await expect(page.getByText('Page 2 of 2')).toBeVisible();
  await expect(page.getByRole('button', { name: 'Previous' })).toBeEnabled();
});

test('the public catalog opens read-only details without device actions', async ({ page }) => {
  await page.goto('/firmware/releases/');
  await expect(page.getByRole('heading', { name: 'XORA 2.0.0' })).toBeVisible();
  const trigger = page.locator('summary').filter({ hasText: 'View details' }).nth(1);
  await trigger.click();
  await expect(trigger.locator('..').getByText('Included components')).toBeVisible();
  await expect(page.getByRole('dialog')).toHaveCount(0);
  await expect(page.getByRole('button', { name: 'Install release' })).toHaveCount(0);
});

test('installed, mixed, and recoverable states keep the current firmware and actions clear', async ({ page }) => {
  await openFirmware(page);
  await page.evaluate(() => {
    sessionStorage.setItem('xora-mock-install', JSON.stringify({
      protocol: 2, deviceModel: 'STM32H750_HBOX', hardwareVersion: '2.0.0', currentSlot: 'A', configVersion: 34,
      securityVersion: 1, metadataConsistent: true,
      stm32: { version: '2.0.0', buildId: 'mock-installed', protocol: 2, maintenance: 2 },
      tx: { version: '2.0.0', buildId: 'mock-installed', protocol: 2, maintenance: 2 },
      installationState: 'installed', confirmedVersion: '2.0.0', confirmedDigest: 'mock-digest',
      sessionId: '', phase: 'completed', targetVersion: '', targetDigest: '', error: '',
      canAbort: false, canRetry: false, txReceived: 0,
    }));
  });
  await page.reload();
  await expect(page.getByText('Verified', { exact: true })).toBeVisible();
  await expect(page.getByText('XORA 2.0.0', { exact: true }).first()).toBeVisible();
  await expect(page.getByRole('button', { name: 'Reinstall' })).toBeVisible();
  await expect(page.getByRole('button', { name: 'Downgrade' })).toBeVisible();

  await page.evaluate(() => {
    const inventory = JSON.parse(sessionStorage.getItem('xora-mock-install') || '{}');
    inventory.installationState = 'mixed';
    inventory.phase = 'failed';
    inventory.tx.version = '1.9.0';
    inventory.sessionId = 'mock-recovery';
    inventory.targetVersion = '2.0.0';
    inventory.canRetry = true;
    inventory.canAbort = true;
    sessionStorage.setItem('xora-mock-install', JSON.stringify(inventory));
  });
  await page.reload();
  await expect(page.getByText('Mixed components')).toBeVisible();
  await expect(page.getByRole('heading', { name: 'Installation needs recovery' })).toBeVisible();
  await expect(page.getByRole('button', { name: 'Retry target release' })).toHaveCount(0);
  await expect(page.getByRole('button', { name: 'Cancel staged installation' })).toBeVisible();
});

for (const width of [1440, 1024, 390]) {
  test(`firmware page has no horizontal overflow at ${width}px`, async ({ page }) => {
    await page.setViewportSize({ width, height: 900 });
    await openFirmware(page);
    const scrollWidth = await page.evaluate(() => document.documentElement.scrollWidth);
    expect(scrollWidth).toBe(width);
    if (width === 390) {
      const trigger = page.locator('summary').filter({ hasText: 'View details' }).nth(1);
      await trigger.click();
      await expect(trigger.locator('..').getByText('Included components')).toBeVisible();
      expect(await page.evaluate(() => document.documentElement.scrollWidth)).toBe(width);
    }
  });
}

test('TX startup failure restores the previous controller and TX in the same progress view', async ({ page }) => {
  await openFirmware(page);
  await page.evaluate(() => sessionStorage.setItem('xora-mock-install-failure', 'tx'));
  await page.getByRole('button', { name: 'Install release' }).nth(1).click();
  const dialog = page.getByRole('dialog');
  await dialog.getByRole('button', { name: 'Confirm installation' }).click();
  await expect(dialog.getByRole('status')).toHaveText('Update failed; previous version restored');
  expect(Number(await dialog.getByRole('progressbar').getAttribute('aria-valuenow'))).toBeGreaterThanOrEqual(80);
  expect(Number(await dialog.getByRole('progressbar').getAttribute('aria-valuenow'))).toBeLessThan(100);
  const state = await page.evaluate(() => JSON.parse(sessionStorage.getItem('xora-mock-install') || '{}'));
  expect(state.currentSlot).toBe('A'); expect(state.stm32.version).toBe('1.0.0'); expect(state.restoreAttempts).toBe(1);
  await expect(dialog.getByTestId('install-step-timeline')).toHaveAttribute('data-step', '2');
  await expect(dialog.getByText('Install update', { exact: true })).toBeVisible();
  await expect(dialog.getByRole('button', { name: 'Retry installation' })).toHaveCount(0);
});

test('refresh resumes the persisted task and restore failure keeps a manual reconnect entry', async ({ page }) => {
  await openFirmware(page);
  await page.evaluate(() => sessionStorage.setItem('xora-mock-install-failure', 'restore'));
  await page.getByRole('button', { name: 'Install release' }).nth(1).click();
  await page.getByRole('dialog').getByRole('button', { name: 'Confirm installation' }).click();
  await expect.poll(() => page.evaluate(() => Boolean(localStorage.getItem('xora-release-install-v2')))).toBe(true);
  const id = await page.evaluate(() => JSON.parse(localStorage.getItem('xora-release-install-v2')!).sessionId);
  await page.reload(); const dialog=page.getByRole('dialog');
  await expect(dialog.getByRole('status')).toHaveText('TX recovery failed: maintenance recovery required');
  await expect(dialog.getByRole('alert')).toContainText('TX_RESTORE_FAILED');
  expect(Number(await dialog.getByRole('progressbar').getAttribute('aria-valuenow'))).toBeGreaterThanOrEqual(80);
  expect(Number(await dialog.getByRole('progressbar').getAttribute('aria-valuenow'))).toBeLessThan(100);
  await expect(dialog.getByRole('button', { name: 'Reconnect device' })).toBeVisible();
  expect(await page.evaluate(() => JSON.parse(localStorage.getItem('xora-release-install-v2')!).sessionId)).toBe(id);
});

test('expired task shows TX connection timeout and late manual read reconciles device completion', async ({ page }) => {
  await openFirmware(page);
  await page.evaluate(() => sessionStorage.setItem('xora-mock-install-failure', 'timeout'));
  await page.getByRole('button', { name: 'Install release' }).nth(1).click();
  await page.getByRole('dialog').getByRole('button', { name: 'Confirm installation' }).click();
  await expect.poll(() => page.evaluate(() => Boolean(localStorage.getItem('xora-release-install-v2')))).toBe(true);
  await page.evaluate(() => {
    const task = JSON.parse(localStorage.getItem('xora-release-install-v2')!);task.activatedAt = Date.now() - 181000;
    localStorage.setItem('xora-release-install-v2',JSON.stringify(task));
  });
  await page.reload();const dialog=page.getByRole('dialog');
  await expect(dialog.getByRole('status')).toHaveText('TX update failed (connection timeout)');
  expect(Number(await dialog.getByRole('progressbar').getAttribute('aria-valuenow'))).toBeGreaterThanOrEqual(80);
  expect(Number(await dialog.getByRole('progressbar').getAttribute('aria-valuenow'))).toBeLessThan(100);
  await expect(dialog.getByRole('button', { name: 'Reconnect device' })).toBeVisible();
  await page.evaluate(() => {
    sessionStorage.removeItem('xora-mock-install-failure');
    const state=JSON.parse(sessionStorage.getItem('xora-mock-install')!);state.offlineStarted=Date.now()-7000;
    sessionStorage.setItem('xora-mock-install',JSON.stringify(state));
  });
  await dialog.getByRole('button', { name: 'Reconnect device' }).click();
  await expect(dialog.getByRole('status')).toHaveText('Update complete');
  await expect(dialog.getByRole('progressbar')).toHaveAttribute('aria-valuenow', '100');
});
