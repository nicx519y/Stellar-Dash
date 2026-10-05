const test = require('node:test');
const assert = require('node:assert/strict');
const { normalizeScreenControl, applyScreenControlPatch } = require('../lib/screen-control-config.ts');
const { DEFAULT_SCREEN_CONTROL_CONFIG } = require('../types/gamepad-config.ts');
const { decodeConfigResource } = require('../lib/device-transport/config-modules.ts');
const { writeConfigResource } = require('../lib/device-transport/config-snapshot.ts');

test('legacy standby and grouped menu migration preserve selection and order', () => {
  const old = {
    standbyDisplay: 'none', standbyTimeoutSeconds: 120, currentPageId: 7,
    features: { ledBrightnessAdjust: false, ledEffectSwitch: true, ambientBrightnessAdjust: false, ambientEffectSwitch: false },
    featuresOrder: ['profilesSwitch', 'ambientEffectSwitch', 'inputModeSwitch', 'ledBrightnessAdjust', 'screenBrightnessAdjust'],
  };
  const migrated = normalizeScreenControl(old);
  assert.equal(migrated.standbyEnabled, false);
  assert.equal(migrated.standbyDisplay, 'screenOff');
  assert.equal(migrated.standbyTimeoutSeconds, 120);
  assert.equal(migrated.currentPageId, 13);
  assert.equal(migrated.features.ledSetting, true);
  assert.equal(migrated.features.power, true);
  assert.deepEqual(migrated.featuresOrder.slice(0, 5), ['profilesSwitch', 'ledSetting', 'inputModeSwitch', 'screenBrightnessAdjust', 'power']);
  assert.equal(migrated.featuresOrder.length, 10);
  assert.deepEqual(normalizeScreenControl(migrated), migrated);
  for (const mode of ['backgroundImage', 'buttonLayout']) assert.equal(normalizeScreenControl({ ...old, standbyDisplay: mode }).standbyEnabled, true);
  const hidden = normalizeScreenControl({ ...old, features: { ...old.features, ledEffectSwitch: false } });
  assert.equal(hidden.features.ledSetting, false);
  assert.equal(hidden.currentPageId, 1);
});

test('screen switch, mode and timeout are independent, and invalid patches are atomic', () => {
  const off = normalizeScreenControl(DEFAULT_SCREEN_CONTROL_CONFIG);
  const preset = applyScreenControlPatch(off, { standbyDisplay: 'buttonLayout', standbyTimeoutSeconds: 300 });
  assert.equal(preset.standbyEnabled, false);
  const on = applyScreenControlPatch(preset, { standbyEnabled: true });
  assert.equal(on.standbyDisplay, 'buttonLayout');
  assert.equal(on.standbyTimeoutSeconds, 300);
  for (const patch of [{ standbyEnabled: 1 }, { standbyDisplay: 'bad' }, { standbyTimeoutSeconds: 15 }, { features: { power: 1 } }]) {
    const before = structuredClone(on);
    assert.throws(() => applyScreenControlPatch(on, patch), /Invalid/);
    assert.deepEqual(on, before);
  }
  const imported = applyScreenControlPatch(on, { standbyDisplay: 'none' }, true);
  assert.equal(imported.standbyEnabled, false);
  const offAgain = applyScreenControlPatch(on, { standbyEnabled: false });
  assert.equal(offAgain.standbyTimeoutSeconds, 300);
  assert.equal(offAgain.standbyDisplay, 'buttonLayout');
});

test('old live firmware remains read-only even after normalization and never sends a write', async () => {
  const old = { ...DEFAULT_SCREEN_CONTROL_CONFIG, standbyDisplay: 'none' };
  delete old.standbyEnabled;
  const decoded = decodeConfigResource('screen-control', old, value => value);
  assert.equal(decoded.standbySupported, false);
  assert.equal(normalizeScreenControl(decoded).standbySupported, false);
  let calls = 0;
  await assert.rejects(writeConfigResource(async () => { ++calls; }, 'screen-control', decoded, value => value), /Update device firmware/);
  assert.equal(calls, 0);
  assert.equal(decodeConfigResource('screen-control', DEFAULT_SCREEN_CONTROL_CONFIG, value => value).standbySupported, true);
});
