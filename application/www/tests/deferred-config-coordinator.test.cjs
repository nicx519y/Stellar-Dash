const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const {
  DeferredConfigCoordinator,
} = require('../lib/deferred-config-coordinator.ts');

test('twenty edits of one resource produce no write until flush and one final commit', async () => {
  const commits = [];
  const coordinator = new DeferredConfigCoordinator();
  for (let index = 0; index < 20; index += 1) {
    coordinator.stage('profile:default', async () => commits.push(index));
  }

  assert.equal(coordinator.dirty, true);
  assert.deepEqual(commits, []);
  await coordinator.flush();
  assert.deepEqual(commits, [19]);
  assert.equal(coordinator.dirty, false);
});

test('profile details commit before macros regardless of staging order', async () => {
  const commits = [];
  const coordinator = new DeferredConfigCoordinator();
  coordinator.stage('macros:default', async () => commits.push('macros'), 20);
  coordinator.stage('profile:default', async () => commits.push('profile'), 10);
  await coordinator.flush();
  assert.deepEqual(commits, ['profile', 'macros']);
});

test('failed commits remain dirty and are retryable', async () => {
  let attempts = 0;
  const coordinator = new DeferredConfigCoordinator();
  coordinator.stage('profile:default', async () => {
    attempts += 1;
    if (attempts === 1) throw new Error('device busy');
  });

  await assert.rejects(coordinator.flush(), /device busy/);
  assert.equal(coordinator.dirty, true);
  await coordinator.flush();
  assert.equal(attempts, 2);
  assert.equal(coordinator.dirty, false);
});

test('button-driven pages stage durable configuration instead of writing it directly', () => {
  const expectations = new Map([
    ['keys-setting-content.tsx', ['stageDeferredProfileDetails', 'stageDeferredProfileMacros']],
    ['leds-setting-content.tsx', ['stageDeferredProfileDetails']],
    ['buttons-performance-setting-content.tsx', ['stageDeferredProfileDetails']],
    ['global-setting-content.tsx', ['stageDeferredHotkeysConfig']],
    ['input-mode-content.tsx', ['stageDeferredGlobalConfig']],
    ['connection-mode-content.tsx', ['stageDeferredGlobalConfig']],
    ['screen-control-setting-content.tsx', ['stageDeferredScreenControl']],
  ]);
  const forbidden = /\b(?:updateProfileDetails|updateProfileMacros|updateHotkeysConfig|updateGlobalConfig|updateScreenControl|sendPendingCommandImmediately)\s*\(/;

  for (const [filename, stagedApis] of expectations) {
    const source = fs.readFileSync(
      path.join(__dirname, '../components', filename),
      'utf8',
    );
    assert.doesNotMatch(source, forbidden, `${filename} bypasses the deferred coordinator`);
    for (const api of stagedApis) assert.match(source, new RegExp(`\\b${api}\\s*\\(`));
  }
});

test('settings navigation paints the destination before starting a background save', () => {
  const settings = fs.readFileSync(
    path.join(__dirname, '../components/settings-layout.tsx'),
    'utf8',
  );
  const route = settings.indexOf('setRoute(details.value as Route)');
  const flush = settings.indexOf('flushDeferredConfig(undefined, true)', route);
  assert.ok(route >= 0 && flush > route);
  assert.match(settings.slice(route, flush), /window\.setTimeout/);
  assert.doesNotMatch(settings, /await flushDeferredConfig\(\(\) => setRoute/);

  const layout = fs.readFileSync(
    path.join(__dirname, '../app/layout.tsx'),
    'utf8',
  );
  assert.doesNotMatch(layout, /connectionPending \|\| showLoading/);
  assert.match(layout, /pointerEvents="none"/);

  const finish = fs.readFileSync(
    path.join(__dirname, '../components/finish-config-button.tsx'),
    'utf8',
  );
  assert.match(finish, /id: 'config-saving'/);
  assert.match(finish, /closeFinishDialog\(savingDialogId\)/);
  assert.match(finish, /await exitWebConfig\(\)/);
  assert.match(finish, /terminateWebConfigActivities/);
  assert.doesNotMatch(finish, /await rebootSystem\(\)/);

  const context = fs.readFileSync(
    path.join(__dirname, '../contexts/gamepad-config-context.tsx'),
    'utf8',
  );
  const suspend = context.indexOf('suspended = await lease.suspend');
  const terminate = context.indexOf('await beforeFlush?.()', suspend);
  const persist = context.indexOf('await deferredConfigRef.current!.flush()', terminate);
  assert.ok(suspend >= 0 && terminate > suspend && persist > terminate);
  for (const command of [
    'stop_button_monitoring',
    'stop_manual_calibration',
    'ms_mark_mapping_stop',
    'clear_leds_preview',
    'import_config_abort',
  ]) {
    assert.match(context, new RegExp(`['"]${command}['"]`));
  }
});

test('profile operations preserve profile content under the shared blocking overlay', () => {
  const profileSelect = fs.readFileSync(
    path.join(__dirname, '../components/profile-select.tsx'),
    'utf8',
  );

  assert.match(
    profileSelect,
    /<LoadingModal isOpen=\{pendingOperation !== null\} variant="operation" \/>/,
  );
  assert.doesNotMatch(profileSelect, /loading=\{pendingOperation === `switch:/);
  assert.doesNotMatch(profileSelect, /<Spinner\b/);
});
