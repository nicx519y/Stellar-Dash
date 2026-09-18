'use strict';

const assert = require('node:assert/strict');
const fs = require('fs-extra');
const os = require('node:os');
const path = require('node:path');
const test = require('node:test');
const Database = require('better-sqlite3');

const {
    DATABASE_VERSION,
    UserAccountStore,
} = require('../src/user-account-store');

function fixture() {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'st-dash-users-'));
    const clock = { value: 1800000000000 };
    const store = new UserAccountStore({
        databasePath: path.join(root, 'user_accounts.sqlite3'),
        now: () => clock.value,
    });
    return { root, clock, store };
}

test('user account migrations are repeatable and enforce uid foreign keys', t => {
    const value = fixture();
    t.after(() => {
        value.store.close();
        fs.removeSync(value.root);
    });

    assert.equal(
        value.store.database.pragma('user_version', { simple: true }),
        DATABASE_VERSION
    );
    assert.equal(
        value.store.database.pragma('foreign_keys', { simple: true }),
        1
    );
    assert.equal(
        value.store.database.pragma('journal_mode', { simple: true }),
        'wal'
    );

    value.store.createEmailVerification({
        tokenHash: 'a'.repeat(64),
        email: 'user@st-dash.com',
        locale: 'en',
        ttlMs: 1000,
    });
    const account = value.store.completeRegistration({
        tokenHash: 'a'.repeat(64),
        passwordHash: '$argon2id$test',
        displayName: 'user',
    });
    assert.match(
        account.uid,
        /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/
    );
    assert.equal(account.email, 'user@st-dash.com');
    assert.equal(account.role, 'user');
    assert.equal(account.avatarId, null);
    const withAvatar = value.store.changeUserAvatar({
        userUid: account.uid,
        avatarId: 'sf6-ryu',
    });
    assert.equal(withAvatar.avatarId, 'sf6-ryu');
    assert.equal(
        value.store.database.prepare(`
            SELECT COUNT(*) AS count FROM account_audit_log
            WHERE action = 'user-avatar-changed' AND target_user_uid = ?
        `).get(account.uid).count,
        1
    );
    assert.equal(
        value.store.completeRegistration({
            tokenHash: 'a'.repeat(64),
            passwordHash: 'ignored',
            displayName: 'ignored',
        }),
        null
    );
    assert.throws(() => value.store.createSession({
        tokenHash: 'b'.repeat(64),
        userUid: '00000000-0000-4000-8000-000000000000',
        ttlMs: 1000,
    }));
});

test('pending role grant is consumed once and the last admin is protected', t => {
    const value = fixture();
    t.after(() => {
        value.store.close();
        fs.removeSync(value.root);
    });

    const grant = value.store.grantRoleByEmail({
        email: 'admin@example.com',
        role: 'admin',
    });
    assert.deepEqual(grant, {
        status: 'pending',
        email: 'admin@example.com',
        role: 'admin',
    });
    value.store.createEmailVerification({
        tokenHash: '9'.repeat(64),
        email: 'admin@example.com',
        locale: 'en',
        ttlMs: 1000,
    });
    const admin = value.store.completeRegistration({
        tokenHash: '9'.repeat(64),
        passwordHash: '$argon2id$admin',
        displayName: 'admin',
    });
    assert.equal(admin.role, 'admin');
    assert.equal(value.store.findPendingRoleGrant('admin@example.com'), null);
    assert.throws(() => value.store.changeUserRole({
        userUid: admin.uid,
        role: 'user',
        actorUid: admin.uid,
    }), error => error.code === 'LAST_ADMIN_REQUIRED');

    value.store.createEmailVerification({
        tokenHash: '8'.repeat(64),
        email: 'second@example.com',
        locale: 'en',
        ttlMs: 1000,
    });
    const second = value.store.completeRegistration({
        tokenHash: '8'.repeat(64),
        passwordHash: '$argon2id$user',
        displayName: 'second',
    });
    assert.equal(second.role, 'user');
    assert.equal(value.store.changeUserRole({
        userUid: second.uid,
        role: 'admin',
        actorUid: admin.uid,
    }).role, 'admin');
    assert.equal(value.store.changeUserRole({
        userUid: admin.uid,
        role: 'user',
        actorUid: second.uid,
    }).role, 'user');
});

test('offline verified account creation consumes its role grant atomically', t => {
    const value = fixture();
    t.after(() => {
        value.store.close();
        fs.removeSync(value.root);
    });
    value.store.grantRoleByEmail({
        email: 'direct@example.com',
        role: 'admin',
    });
    const result = value.store.createVerifiedEmailAccount({
        email: 'direct@example.com',
        passwordHash: '$argon2id$direct-fixture',
        displayName: 'direct',
        role: 'admin',
    });
    assert.equal(result.status, 'created');
    assert.equal(result.user.role, 'admin');
    assert.equal(result.user.email, 'direct@example.com');
    assert.equal(value.store.findPendingRoleGrant('direct@example.com'), null);
    assert.equal(
        value.store.findCredentialByEmail('direct@example.com').passwordHash,
        '$argon2id$direct-fixture'
    );
    assert.equal(value.store.createVerifiedEmailAccount({
        email: 'direct@example.com',
        passwordHash: '$argon2id$must-not-replace',
        displayName: 'other',
        role: 'user',
    }).status, 'exists');
    assert.equal(
        value.store.findCredentialByEmail('direct@example.com').passwordHash,
        '$argon2id$direct-fixture'
    );
});

test('expired verification, captcha and session state is rejected and cleaned', t => {
    const value = fixture();
    t.after(() => {
        value.store.close();
        fs.removeSync(value.root);
    });

    value.store.createEmailVerification({
        tokenHash: 'c'.repeat(64),
        email: 'expired@example.com',
        locale: 'zh',
        ttlMs: 10,
    });
    value.store.createCaptcha({
        challengeId: '11111111-1111-4111-8111-111111111111',
        action: 'login',
        answerHash: 'd'.repeat(64),
        ttlMs: 10,
    });
    value.clock.value += 11;
    assert.equal(value.store.findPendingEmailVerification('c'.repeat(64)), null);
    assert.equal(value.store.consumeCaptcha({
        challengeId: '11111111-1111-4111-8111-111111111111',
        action: 'login',
    }), null);
    const removed = value.store.cleanupExpired();
    assert.equal(removed.verifications, 1);
    assert.equal(removed.captchas, 1);
});

test('legacy OAuth v1 database is upgraded without deleting account data', t => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'st-dash-oauth-v1-'));
    const databasePath = path.join(root, 'user_accounts.sqlite3');
    const legacy = new Database(databasePath);
    legacy.pragma('foreign_keys = ON');
    legacy.exec(`
        CREATE TABLE users (
            uid TEXT PRIMARY KEY,
            display_name TEXT NOT NULL,
            avatar_path TEXT,
            created_at INTEGER NOT NULL,
            updated_at INTEGER NOT NULL,
            last_login_at INTEGER NOT NULL
        );
        CREATE TABLE oauth_identities (
            provider TEXT NOT NULL,
            provider_subject TEXT NOT NULL,
            user_uid TEXT NOT NULL UNIQUE,
            FOREIGN KEY (user_uid) REFERENCES users(uid) ON DELETE CASCADE
        );
        CREATE TABLE user_sessions (
            token_hash TEXT PRIMARY KEY,
            user_uid TEXT NOT NULL,
            created_at INTEGER NOT NULL,
            expires_at INTEGER NOT NULL,
            FOREIGN KEY (user_uid) REFERENCES users(uid) ON DELETE CASCADE
        );
        INSERT INTO users (
            uid, display_name, avatar_path, created_at, updated_at, last_login_at
        ) VALUES (
            '11111111-1111-4111-8111-111111111111',
            'Legacy User', 'avatar.png', 100, 100, 100
        );
        INSERT INTO oauth_identities (
            provider, provider_subject, user_uid
        ) VALUES (
            'google', 'legacy-google-sub',
            '11111111-1111-4111-8111-111111111111'
        );
        INSERT INTO user_sessions (
            token_hash, user_uid, created_at, expires_at
        ) VALUES (
            '${'f'.repeat(64)}',
            '11111111-1111-4111-8111-111111111111', 100, 200
        );
        PRAGMA user_version = 1;
    `);
    legacy.close();

    const store = new UserAccountStore({
        databasePath,
        now: () => 150,
    });
    t.after(() => {
        store.close();
        fs.removeSync(root);
    });

    assert.equal(
        store.database.pragma('user_version', { simple: true }),
        DATABASE_VERSION
    );
    assert.equal(
        store.database.prepare(
            'SELECT COUNT(*) AS count FROM email_credentials'
        ).get().count,
        0
    );
    assert.deepEqual(
        store.database.prepare(`
            SELECT provider, provider_subject AS providerSubject
            FROM oauth_identities
        `).get(),
        { provider: 'google', providerSubject: 'legacy-google-sub' }
    );
    assert.deepEqual(
        store.database.prepare(`
            SELECT issued_at AS issuedAt
            FROM user_sessions
        `).get(),
        { issuedAt: 100 }
    );
    const sessionColumns = store.database.pragma('table_info(user_sessions)')
        .map(column => column.name);
    assert.equal(sessionColumns.includes('created_at'), false);
    assert.equal(sessionColumns.includes('issued_at'), true);
    assert.equal(
        store.database.pragma('table_info(users)')
            .some(column => column.name === 'avatar_id'),
        true
    );
    assert.doesNotThrow(() => store.createSession({
        tokenHash: 'd'.repeat(64),
        userUid: '11111111-1111-4111-8111-111111111111',
        ttlMs: 60000,
    }));

    store.createEmailVerification({
        tokenHash: 'e'.repeat(64),
        email: 'new@st-dash.com',
        locale: 'en',
        ttlMs: 1000,
    });
    assert.ok(store.completeRegistration({
        tokenHash: 'e'.repeat(64),
        passwordHash: '$argon2id$test',
        displayName: 'new',
    }));
});
