#!/usr/bin/env node

const assert = require('assert');
const fs = require('fs-extra');
const os = require('os');
const path = require('path');

const { findNewerFirmwares } = require('../src/action');
const { FirmwareStorage } = require('../src/firmware');

const candidates = [
    { id: 'v1', version: '2.0.0', hardwareVersion: '1.0.0' },
    { id: 'v2', version: '2.0.0', hardwareVersion: '2.0.0' },
    { id: 'legacy', version: '9.0.0' }
];
assert.deepStrictEqual(
    findNewerFirmwares('1.0.0', '1.0.0', candidates).map(item => item.id),
    ['v1']
);
assert.deepStrictEqual(
    findNewerFirmwares('1.0.0', '2.0.0', candidates).map(item => item.id),
    ['v2']
);
assert.deepStrictEqual(findNewerFirmwares('1.0.0', undefined, candidates), []);

const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'hbox-ota-gate-'));
try {
    const storage = new FirmwareStorage(
        path.join(temp, 'firmwares.json'),
        path.join(temp, 'uploads')
    );
    fs.ensureDirSync(path.join(temp, 'uploads'));
    assert.strictEqual(storage.addFirmware({
        name: 'V1',
        version: '3.0.0',
        hardwareVersion: '1.0.0'
    }), true);
    assert.strictEqual(storage.addFirmware({
        name: 'V2',
        version: '3.0.0',
        hardwareVersion: '2.0.0'
    }), true);
    assert.strictEqual(storage.addFirmware({
        name: 'duplicate V2',
        version: '3.0.0',
        hardwareVersion: '2.0.0'
    }), false);
    assert.strictEqual(storage.data.firmwares.length, 2);
} finally {
    fs.removeSync(temp);
}

console.log('OTA hardware gate tests passed');
