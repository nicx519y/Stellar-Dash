'use strict';

const assert = require('node:assert/strict');
const fs = require('fs-extra');
const os = require('node:os');
const path = require('node:path');
const test = require('node:test');

const {
    DATABASE_VERSION,
    DeviceAccountStore,
} = require('../src/device-account-store');

function createFixture() {
    const root = fs.mkdtempSync(
        path.join(os.tmpdir(), 'hbox-device-accounts-')
    );
    const databasePath = path.join(root, 'accounts.sqlite3');
    const clock = { value: 1800000000000 };
    const store = new DeviceAccountStore({
        databasePath,
        now: () => clock.value,
    });
    return { root, databasePath, clock, store };
}

test('first verified device registration creates a UUIDv4 account and is idempotent', t => {
    const fixture = createFixture();
    t.after(() => {
        fixture.store.close();
        fs.removeSync(fixture.root);
    });

    const first = fixture.store.findOrCreateDeviceAccount({
        deviceId: '00112233445566778899aabbccddeeff',
        displayName: 'Controller One',
    });
    assert.equal(first.created, true);
    assert.match(
        first.uid,
        /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/
    );
    assert.equal(first.deviceId, '00112233445566778899AABBCCDDEEFF');

    fixture.clock.value += 1000;
    const repeat = fixture.store.findOrCreateDeviceAccount({
        deviceId: '00112233445566778899AABBCCDDEEFF',
        displayName: 'Renamed Controller',
    });
    assert.equal(repeat.created, false);
    assert.equal(repeat.uid, first.uid);
    assert.equal(repeat.displayName, 'Renamed Controller');
    assert.equal(repeat.lastSeenAt, fixture.clock.value);
});

test('different devices receive different accounts and future data references account uid', t => {
    const fixture = createFixture();
    t.after(() => {
        fixture.store.close();
        fs.removeSync(fixture.root);
    });

    const first = fixture.store.findOrCreateDeviceAccount({
        deviceId: '00112233445566778899AABBCCDDEEFF',
    });
    const second = fixture.store.findOrCreateDeviceAccount({
        deviceId: 'FFEEDDCCBBAA99887766554433221100',
    });
    assert.notEqual(first.uid, second.uid);

    fixture.store.database.exec(`
        CREATE TABLE future_account_feature (
            account_uid TEXT NOT NULL,
            value TEXT NOT NULL,
            FOREIGN KEY (account_uid) REFERENCES accounts(uid)
        )
    `);
    fixture.store.database.prepare(`
        INSERT INTO future_account_feature (account_uid, value)
        VALUES (?, ?)
    `).run(first.uid, 'allowed');
    assert.throws(() => fixture.store.database.prepare(`
        INSERT INTO future_account_feature (account_uid, value)
        VALUES (?, ?)
    `).run('00000000-0000-4000-8000-000000000000', 'denied'));
});

test('database settings, validation and repeated migration fail safely', () => {
    const fixture = createFixture();
    try {
        assert.equal(
            fixture.store.database.pragma('user_version', { simple: true }),
            DATABASE_VERSION
        );
        assert.equal(
            fixture.store.database.pragma('foreign_keys', { simple: true }),
            1
        );
        assert.equal(
            fixture.store.database.pragma('journal_mode', { simple: true }),
            'wal'
        );
        assert.throws(
            () => fixture.store.findOrCreateDeviceAccount({ deviceId: 'spoofed' }),
            /16-byte hexadecimal/
        );
        const persisted = fixture.store.findOrCreateDeviceAccount({
            deviceId: '00112233445566778899AABBCCDDEEFF',
            displayName: 'Persistent Controller',
        });
        fixture.store.close();

        fixture.store = new DeviceAccountStore({
            databasePath: fixture.databasePath,
            now: () => fixture.clock.value,
        });
        assert.equal(
            fixture.store.findByDeviceId(
                '00112233445566778899AABBCCDDEEFF'
            ).uid,
            persisted.uid
        );
    } finally {
        fixture.store.close();
        fs.removeSync(fixture.root);
    }
});
