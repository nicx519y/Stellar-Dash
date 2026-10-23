const test = require('node:test');
const assert = require('node:assert/strict');
const { deviceDisplayText } = require('../lib/device-display-text.ts');

test('chip diagnostics use localized component roles, including explicit TX and RX', () => {
  const message = 'STM32H750 ↔ CH585 TX / CH585 RX';
  assert.equal(deviceDisplayText(message, 'zh'), '主控 ↔ TX / RX');
  assert.equal(deviceDisplayText(message, 'en'), 'Main controller ↔ TX / RX');
  assert.equal(deviceDisplayText('stm32 / ch585', 'en'), 'Main controller / TX');
});

test('role labels and unrelated diagnostic details retain their content', () => {
  const message = 'Main controller / TX / RX: protocol=2, build=unidentified, 0x1000';
  assert.equal(deviceDisplayText(message, 'en'), message);
  assert.equal(deviceDisplayText('主控 ↔ TX', 'zh'), '主控 ↔ TX');
});
