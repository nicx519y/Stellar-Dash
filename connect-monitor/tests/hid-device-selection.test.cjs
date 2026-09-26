const assert = require('node:assert/strict');
const test = require('node:test');

const {
  matchesHidTelemetryDevice,
} = require('../dist/electron/sources/hid-device-selection.js');

const automaticTarget = { vendorId: null, productId: null };

function device(overrides) {
  return {
    vendorId: 0x1a86,
    productId: 0xfe0c,
    usagePage: 0xff00,
    usage: 0x01,
    interface: 3,
    manufacturer: 'HBox RF',
    product: 'HBox XInput + CDC Dongle',
    ...overrides,
  };
}

test('accepts known HBox telemetry interfaces', () => {
  assert.equal(matchesHidTelemetryDevice(device({}), automaticTarget), true);
  assert.equal(
    matchesHidTelemetryDevice(
      device({ vendorId: 0x045e, productId: 0x02ff }),
      automaticTarget,
    ),
    true,
  );
});

test('rejects the dedicated WebConfig HID interface', () => {
  const webConfig = device({
    vendorId: 0xcafe,
    productId: 0x4021,
    interface: 0,
    manufacturer: 'HBox',
    product: 'HBox WebConfig',
    serialNumber: 'HBOX-WEBCONFIG-V2',
  });

  assert.equal(matchesHidTelemetryDevice(webConfig, automaticTarget), false);
  assert.equal(
    matchesHidTelemetryDevice(
      webConfig,
      { vendorId: 0xcafe, productId: 0x4021 },
    ),
    false,
  );
});

test('does not treat an arbitrary HBox vendor HID as telemetry', () => {
  assert.equal(
    matchesHidTelemetryDevice(
      device({
        vendorId: 0x1234,
        productId: 0xabcd,
        product: 'HBox Prototype',
      }),
      automaticTarget,
    ),
    false,
  );
});

test('explicit bring-up IDs still require a telemetry interface', () => {
  const target = { vendorId: 0x1234, productId: 0xabcd };
  assert.equal(
    matchesHidTelemetryDevice(
      device({
        vendorId: 0x1234,
        productId: 0xabcd,
        product: 'Prototype Telemetry',
      }),
      target,
    ),
    true,
  );
  assert.equal(
    matchesHidTelemetryDevice(
      device({
        vendorId: 0x1234,
        productId: 0xabcd,
        usagePage: 0x01,
        usage: 0x05,
        interface: 0,
      }),
      target,
    ),
    false,
  );
});

test('explicit target fields must match', () => {
  assert.equal(
    matchesHidTelemetryDevice(
      device({}),
      { vendorId: 0x045e, productId: 0x02ff },
    ),
    false,
  );
});
