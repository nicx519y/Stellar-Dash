'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const { createDirectDeviceAccess } = require('../src/direct-device-access');

test('WebConfig resource access does not require a device bearer token', () => {
    const access = createDirectDeviceAccess({ webConfigTargetPolicy: {} });
    const request = { headers: {} };
    let nextCalled = false;
    access.requireSession(['firmware.update'])(request, null, () => {
        nextCalled = true;
    });
    assert.equal(nextCalled, true);
    assert.equal(request.deviceSession.hardwareVersion, '2.0.0');
    assert.equal(request.deviceSession.accountUid, null);
    assert.ok(request.deviceSession.scopes.includes('firmware.update'));
});
