'use strict';

const crypto = require('crypto');
const fs = require('fs-extra');
const path = require('path');
const Database = require('better-sqlite3');

const DATABASE_VERSION = 1;
const DEVICE_ID_PATTERN = /^[0-9A-F]{32}$/;

function unixTimeMs(value = Date.now()) {
    if (!Number.isFinite(value)) {
        throw new TypeError('timestamp must be finite');
    }
    return Math.trunc(value);
}

function normalizeDeviceId(value) {
    if (typeof value !== 'string') {
        throw new TypeError('deviceId is required');
    }
    const normalized = value.trim().toUpperCase();
    if (!DEVICE_ID_PATTERN.test(normalized)) {
        throw new TypeError('deviceId must be a 16-byte hexadecimal identifier');
    }
    return normalized;
}

function normalizeDisplayName(value, deviceId) {
    const normalized = typeof value === 'string' ? value.trim() : '';
    return (normalized || `Device ${deviceId.substring(0, 8)}`).slice(0, 128);
}

class DeviceAccountStore {
    constructor(options) {
        if (!options || typeof options.databasePath !== 'string') {
            throw new TypeError('device account database path is required');
        }
        this.databasePath = path.resolve(options.databasePath);
        fs.ensureDirSync(path.dirname(this.databasePath));
        this.now = options.now || Date.now;
        this.database = new Database(this.databasePath);
        this.database.pragma('foreign_keys = ON');
        this.database.pragma('journal_mode = WAL');
        this.database.pragma('busy_timeout = 5000');
        this.migrate();
        this.prepareStatements();
    }

    migrate() {
        const version = this.database.pragma('user_version', { simple: true });
        if (version > DATABASE_VERSION) {
            throw new Error(
                `device account database version ${version} is newer than supported version ${DATABASE_VERSION}`
            );
        }
        if (version === DATABASE_VERSION) {
            return;
        }

        this.database.transaction(() => {
            if (version < 1) {
                this.database.exec(`
                    CREATE TABLE accounts (
                        uid TEXT PRIMARY KEY,
                        display_name TEXT NOT NULL,
                        created_at INTEGER NOT NULL,
                        updated_at INTEGER NOT NULL,
                        last_seen_at INTEGER NOT NULL
                    );

                    CREATE TABLE device_identities (
                        device_id TEXT PRIMARY KEY,
                        account_uid TEXT NOT NULL UNIQUE,
                        created_at INTEGER NOT NULL,
                        updated_at INTEGER NOT NULL,
                        CHECK (
                            length(device_id) = 32 AND
                            device_id = upper(device_id) AND
                            device_id NOT GLOB '*[^0-9A-F]*'
                        ),
                        FOREIGN KEY (account_uid)
                            REFERENCES accounts(uid) ON DELETE CASCADE
                    );
                `);
            }
            this.database.pragma(`user_version = ${DATABASE_VERSION}`);
        })();
    }

    prepareStatements() {
        this.statements = {
            findByDeviceId: this.database.prepare(`
                SELECT
                    a.uid,
                    a.display_name AS displayName,
                    a.created_at AS createdAt,
                    a.updated_at AS updatedAt,
                    a.last_seen_at AS lastSeenAt,
                    d.device_id AS deviceId
                FROM device_identities d
                JOIN accounts a ON a.uid = d.account_uid
                WHERE d.device_id = ?
            `),
            insertAccount: this.database.prepare(`
                INSERT INTO accounts (
                    uid, display_name, created_at, updated_at, last_seen_at
                ) VALUES (?, ?, ?, ?, ?)
            `),
            insertDeviceIdentity: this.database.prepare(`
                INSERT INTO device_identities (
                    device_id, account_uid, created_at, updated_at
                ) VALUES (?, ?, ?, ?)
            `),
            updateAccountSeen: this.database.prepare(`
                UPDATE accounts
                SET display_name = ?, updated_at = ?, last_seen_at = ?
                WHERE uid = ?
            `),
            updateIdentitySeen: this.database.prepare(`
                UPDATE device_identities SET updated_at = ?
                WHERE device_id = ?
            `),
        };

        this.registerTransaction = this.database.transaction(identity => {
            const existing = this.statements.findByDeviceId.get(
                identity.deviceId
            );
            const now = unixTimeMs(this.now());
            if (existing) {
                this.statements.updateAccountSeen.run(
                    identity.displayName,
                    now,
                    now,
                    existing.uid
                );
                this.statements.updateIdentitySeen.run(now, identity.deviceId);
                return {
                    ...existing,
                    displayName: identity.displayName,
                    updatedAt: now,
                    lastSeenAt: now,
                    created: false,
                };
            }

            const uid = crypto.randomUUID();
            this.statements.insertAccount.run(
                uid,
                identity.displayName,
                now,
                now,
                now
            );
            this.statements.insertDeviceIdentity.run(
                identity.deviceId,
                uid,
                now,
                now
            );
            return {
                uid,
                deviceId: identity.deviceId,
                displayName: identity.displayName,
                createdAt: now,
                updatedAt: now,
                lastSeenAt: now,
                created: true,
            };
        });
    }

    findOrCreateDeviceAccount(identity) {
        if (!identity || typeof identity !== 'object') {
            throw new TypeError('device identity is required');
        }
        const deviceId = normalizeDeviceId(identity.deviceId);
        return this.registerTransaction.immediate({
            deviceId,
            displayName: normalizeDisplayName(identity.displayName, deviceId),
        });
    }

    findByDeviceId(deviceId) {
        return this.statements.findByDeviceId.get(
            normalizeDeviceId(deviceId)
        ) || null;
    }

    close() {
        this.database.close();
    }
}

module.exports = {
    DATABASE_VERSION,
    DeviceAccountStore,
    normalizeDeviceId,
};
