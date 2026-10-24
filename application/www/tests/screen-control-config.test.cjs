const test = require('node:test');
const assert = require('node:assert/strict');
const { normalizeScreenControl, applyScreenControlPatch } = require('../lib/screen-control-config.ts');
const { DEFAULT_SCREEN_CONTROL_CONFIG } = require('../types/gamepad-config.ts');
const { decodeConfigResource } = require('../lib/device-transport/config-modules.ts');
const { writeConfigResource } = require('../lib/device-transport/config-snapshot.ts');
const fs = require('node:fs');
const path = require('node:path');
const { SCREEN_FEATURE_IDS, SCREEN_LANGUAGE_MENU_ID } = require('../lib/screen-control-config.ts');
const { UI_TEXT, UI_TEXT_ZH } = require('../types/gamepad-config.ts');

test('web menu labels, IDs and default order match the current LCD catalog', () => {
  const lcd = fs.readFileSync(path.join(__dirname, '../../Src/display/screen_control/spi_screen_main_list.cpp'), 'utf8');
  const zh = JSON.parse(fs.readFileSync(path.join(__dirname, '../../Resources/fonts/chinese/screen_zh.json'), 'utf8'));
  const ui = fs.readFileSync(path.join(__dirname, '../components/screen-control-setting-content.tsx'), 'utf8');
  const labels = Object.fromEntries([...ui.matchAll(/(\w+): t\.(SETTINGS_SCREEN_CONTROL_FEATURE_\w+),/g)].map(m => [m[1], m[2]]));
  const deviceLabels = new Map([...lcd.matchAll(/\{(\d+), SCREEN_FEATURE_\w+, "([^"]+)"\}/g)].map(m => [Number(m[1]), m[2]]));
  assert.equal(deviceLabels.size, 11);
  for (const [key, id] of Object.entries(SCREEN_FEATURE_IDS)) {
    const english = deviceLabels.get(id);
    assert.equal(UI_TEXT[labels[key]], english, key);
    assert.equal(UI_TEXT_ZH[labels[key]], zh[english] ?? english, key);
  }
  assert.deepEqual(DEFAULT_SCREEN_CONTROL_CONFIG.featuresOrder.map(key => SCREEN_FEATURE_IDS[key]), [15,3,0,1,2,11,13,8,14,9,10]);
  assert.equal(SCREEN_LANGUAGE_MENU_ID, 20);
  assert.equal(UI_TEXT_ZH.SETTINGS_SCREEN_CONTROL_FEATURE_LANGUAGE, '语言');
});

test('LCD language survives live reads and patches, with compatible legacy imports', () => {
  const chinese = normalizeScreenControl({ ...DEFAULT_SCREEN_CONTROL_CONFIG, language: 'zh-CN', currentPageId: 20 }, true);
  assert.equal(chinese.language, 'zh-CN');
  assert.equal(chinese.currentPageId, 20);
  assert.equal(applyScreenControlPatch(chinese, { brightness: 30 }).language, 'zh-CN');
  assert.equal(applyScreenControlPatch(chinese, { brightness: 30 }, true).language, 'en');
  assert.equal(applyScreenControlPatch(chinese, { language: 'zh-CN' }, true).language, 'zh-CN');
  assert.equal(normalizeScreenControl({}).language, 'en');
  assert.throws(() => applyScreenControlPatch(chinese, { language: 'xx' }), /Invalid screen language/);
  assert.equal(chinese.language, 'zh-CN');
});

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
  assert.equal(migrated.featuresOrder.length, 11);
  assert.equal(migrated.featuresOrder.filter(key => key === 'transportModeSwitch').length, 1);
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
