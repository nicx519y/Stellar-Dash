'use strict';

const crypto = require('crypto');
const fs = require('fs-extra');
const path = require('path');
const Database = require('better-sqlite3');

const DATABASE_VERSION = 4;
const ACCOUNT_ROLES = Object.freeze(['admin', 'user']);

function normalizeRole(value) {
    if (!ACCOUNT_ROLES.includes(value)) {
        throw new TypeError('account role must be admin or user');
    }
    return value;
}

function unixTimeMs(value = Date.now()) {
    if (!Number.isFinite(value)) {
        throw new TypeError('timestamp must be finite');
    }
    return Math.trunc(value);
}

class UserAccountStore {
    constructor(options) {
        if (!options || typeof options.databasePath !== 'string') {
            throw new TypeError('user account database path is required');
        }
        this.databasePath = path.resolve(options.databasePath);
        this.now = options.now || Date.now;
        fs.ensureDirSync(path.dirname(this.databasePath));
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
                `user account database version ${version} is newer than supported version ${DATABASE_VERSION}`
            );
        }
        this.database.transaction(() => {
            this.database.exec(`
                CREATE TABLE IF NOT EXISTS users (
                    uid TEXT PRIMARY KEY,
                    display_name TEXT NOT NULL,
                    created_at INTEGER NOT NULL,
                    updated_at INTEGER NOT NULL,
                    last_login_at INTEGER
                );

                CREATE TABLE IF NOT EXISTS email_credentials (
                    user_uid TEXT PRIMARY KEY,
                    email TEXT NOT NULL UNIQUE,
                    password_hash TEXT NOT NULL,
                    verified_at INTEGER NOT NULL,
                    created_at INTEGER NOT NULL,
                    updated_at INTEGER NOT NULL,
                    FOREIGN KEY (user_uid)
                        REFERENCES users(uid) ON DELETE CASCADE
                );

                CREATE TABLE IF NOT EXISTS email_verification_tokens (
                    token_hash TEXT PRIMARY KEY,
                    email TEXT NOT NULL,
                    locale TEXT NOT NULL,
                    created_at INTEGER NOT NULL,
                    expires_at INTEGER NOT NULL,
                    consumed_at INTEGER
                );
                CREATE INDEX IF NOT EXISTS email_verification_email_idx
                    ON email_verification_tokens(email);
                CREATE INDEX IF NOT EXISTS email_verification_expiry_idx
                    ON email_verification_tokens(expires_at);

                CREATE TABLE IF NOT EXISTS user_sessions (
                    token_hash TEXT PRIMARY KEY,
                    user_uid TEXT NOT NULL,
                    issued_at INTEGER NOT NULL,
                    expires_at INTEGER NOT NULL,
                    FOREIGN KEY (user_uid)
                        REFERENCES users(uid) ON DELETE CASCADE
                );

                CREATE TABLE IF NOT EXISTS auth_captchas (
                    challenge_id TEXT PRIMARY KEY,
                    action TEXT NOT NULL,
                    answer_hash TEXT NOT NULL,
                    created_at INTEGER NOT NULL,
                    expires_at INTEGER NOT NULL,
                    consumed_at INTEGER,
                    CHECK (action IN ('register', 'login'))
                );
                CREATE INDEX IF NOT EXISTS auth_captchas_expiry_idx
                    ON auth_captchas(expires_at);
            `);

            const userColumns = this.tableColumns('users');
            this.requireColumns('users', userColumns, [
                'uid',
                'display_name',
                'created_at',
                'updated_at',
                'last_login_at',
            ]);
            if (!userColumns.has('role')) {
                this.database.exec(`
                    ALTER TABLE users
                    ADD COLUMN role TEXT NOT NULL DEFAULT 'user'
                    CHECK (role IN ('admin', 'user'))
                `);
            }
            if (!userColumns.has('avatar_id')) {
                this.database.exec('ALTER TABLE users ADD COLUMN avatar_id TEXT');
            }

            const sessionColumns = this.tableColumns('user_sessions');
            this.requireColumns('user_sessions', sessionColumns, [
                'token_hash',
                'user_uid',
                'expires_at',
            ]);
            if (sessionColumns.has('created_at')) {
                // The former Google/WeChat v1 schema made created_at NOT NULL.
                // Adding issued_at alone would still make new email sessions
                // fail. Rebuild to the canonical v3 shape while retaining all
                // legacy sessions.
                const issuedAtExpression = sessionColumns.has('issued_at')
                    ? 'COALESCE(issued_at, created_at, expires_at)'
                    : 'COALESCE(created_at, expires_at)';
                this.database.exec(`
                    CREATE TABLE user_sessions_v3 (
                        token_hash TEXT PRIMARY KEY,
                        user_uid TEXT NOT NULL,
                        issued_at INTEGER NOT NULL,
                        expires_at INTEGER NOT NULL,
                        FOREIGN KEY (user_uid)
                            REFERENCES users(uid) ON DELETE CASCADE
                    );
                    INSERT INTO user_sessions_v3 (
                        token_hash, user_uid, issued_at, expires_at
                    )
                    SELECT token_hash, user_uid,
                        ${issuedAtExpression}, expires_at
                    FROM user_sessions;
                    DROP TABLE user_sessions;
                    ALTER TABLE user_sessions_v3 RENAME TO user_sessions;
                `);
            } else if (!sessionColumns.has('issued_at')) {
                this.database.exec(
                    'ALTER TABLE user_sessions ADD COLUMN issued_at INTEGER'
                );
                this.database.exec(`
                    UPDATE user_sessions
                    SET issued_at = expires_at
                    WHERE issued_at IS NULL
                `);
            }

            this.database.exec(`
                CREATE INDEX IF NOT EXISTS user_sessions_user_idx
                    ON user_sessions(user_uid);
                CREATE INDEX IF NOT EXISTS user_sessions_expiry_idx
                    ON user_sessions(expires_at);

                CREATE TABLE IF NOT EXISTS pending_role_grants (
                    email TEXT PRIMARY KEY,
                    role TEXT NOT NULL CHECK (role IN ('admin', 'user')),
                    created_at INTEGER NOT NULL,
                    updated_at INTEGER NOT NULL,
                    consumed_at INTEGER,
                    consumed_user_uid TEXT,
                    FOREIGN KEY (consumed_user_uid)
                        REFERENCES users(uid) ON DELETE SET NULL
                );
                CREATE INDEX IF NOT EXISTS pending_role_grants_status_idx
                    ON pending_role_grants(consumed_at, email);

                CREATE TABLE IF NOT EXISTS service_tokens (
                    id TEXT PRIMARY KEY,
                    name TEXT NOT NULL,
                    token_hash TEXT NOT NULL UNIQUE,
                    scopes_json TEXT NOT NULL,
                    created_by_user_uid TEXT NOT NULL,
                    created_at INTEGER NOT NULL,
                    expires_at INTEGER NOT NULL,
                    revoked_at INTEGER,
                    last_used_at INTEGER,
                    FOREIGN KEY (created_by_user_uid)
                        REFERENCES users(uid) ON DELETE RESTRICT
                );
                CREATE INDEX IF NOT EXISTS service_tokens_status_idx
                    ON service_tokens(revoked_at, expires_at);

                CREATE TABLE IF NOT EXISTS account_audit_log (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    actor_type TEXT NOT NULL
                        CHECK (actor_type IN ('user', 'service', 'system')),
                    actor_id TEXT NOT NULL,
                    action TEXT NOT NULL,
                    target_user_uid TEXT,
                    details_json TEXT NOT NULL,
                    created_at INTEGER NOT NULL,
                    FOREIGN KEY (target_user_uid)
                        REFERENCES users(uid) ON DELETE SET NULL
                );
                CREATE INDEX IF NOT EXISTS account_audit_created_idx
                    ON account_audit_log(created_at DESC);
            `);
            this.database.pragma(`user_version = ${DATABASE_VERSION}`);
        })();
    }

    tableColumns(tableName) {
        const allowedTables = new Set([
            'users',
            'email_credentials',
            'email_verification_tokens',
            'user_sessions',
            'auth_captchas',
            'pending_role_grants',
            'service_tokens',
            'account_audit_log',
        ]);
        if (!allowedTables.has(tableName)) {
            throw new Error(`unsupported account table: ${tableName}`);
        }
        return new Set(
            this.database.pragma(`table_info(${tableName})`)
                .map(column => column.name)
        );
    }

    requireColumns(tableName, actualColumns, requiredColumns) {
        const missing = requiredColumns.filter(
            column => !actualColumns.has(column)
        );
        if (missing.length > 0) {
            throw new Error(
                `unsupported ${tableName} schema; missing columns: ` +
                missing.join(', ')
            );
        }
    }

    prepareStatements() {
        this.statements = {
            findCredentialByEmail: this.database.prepare(`
                SELECT
                    u.uid,
                    u.display_name AS displayName,
                    u.created_at AS createdAt,
                    u.updated_at AS updatedAt,
                    u.last_login_at AS lastLoginAt,
                    u.role,
                    u.avatar_id AS avatarId,
                    c.email,
                    c.password_hash AS passwordHash,
                    c.verified_at AS verifiedAt
                FROM email_credentials c
                JOIN users u ON u.uid = c.user_uid
                WHERE c.email = ?
            `),
            insertVerification: this.database.prepare(`
                INSERT INTO email_verification_tokens (
                    token_hash, email, locale, created_at, expires_at
                ) VALUES (?, ?, ?, ?, ?)
            `),
            deletePendingVerifications: this.database.prepare(`
                DELETE FROM email_verification_tokens
                WHERE email = ? AND consumed_at IS NULL
            `),
            deleteVerification: this.database.prepare(`
                DELETE FROM email_verification_tokens WHERE token_hash = ?
            `),
            findVerification: this.database.prepare(`
                SELECT token_hash AS tokenHash, email, locale,
                    created_at AS createdAt, expires_at AS expiresAt,
                    consumed_at AS consumedAt
                FROM email_verification_tokens
                WHERE token_hash = ?
            `),
            consumeVerification: this.database.prepare(`
                UPDATE email_verification_tokens
                SET consumed_at = ?
                WHERE token_hash = ? AND consumed_at IS NULL
            `),
            insertUser: this.database.prepare(`
                INSERT INTO users (
                    uid, display_name, role, created_at, updated_at, last_login_at
                ) VALUES (?, ?, ?, ?, ?, ?)
            `),
            insertCredential: this.database.prepare(`
                INSERT INTO email_credentials (
                    user_uid, email, password_hash, verified_at,
                    created_at, updated_at
                ) VALUES (?, ?, ?, ?, ?, ?)
            `),
            updateLastLogin: this.database.prepare(`
                UPDATE users
                SET last_login_at = ?, updated_at = ?
                WHERE uid = ?
            `),
            insertSession: this.database.prepare(`
                INSERT INTO user_sessions (
                    token_hash, user_uid, issued_at, expires_at
                ) VALUES (?, ?, ?, ?)
            `),
            findSession: this.database.prepare(`
                SELECT
                    s.token_hash AS tokenHash,
                    s.issued_at AS issuedAt,
                    s.expires_at AS expiresAt,
                    u.uid,
                    u.display_name AS displayName,
                    u.role,
                    u.avatar_id AS avatarId,
                    c.email
                FROM user_sessions s
                JOIN users u ON u.uid = s.user_uid
                JOIN email_credentials c ON c.user_uid = u.uid
                WHERE s.token_hash = ?
            `),
            revokeSession: this.database.prepare(`
                DELETE FROM user_sessions WHERE token_hash = ?
            `),
            insertCaptcha: this.database.prepare(`
                INSERT INTO auth_captchas (
                    challenge_id, action, answer_hash, created_at, expires_at
                ) VALUES (?, ?, ?, ?, ?)
            `),
            findCaptcha: this.database.prepare(`
                SELECT challenge_id AS challengeId, action, answer_hash AS answerHash,
                    expires_at AS expiresAt, consumed_at AS consumedAt
                FROM auth_captchas WHERE challenge_id = ?
            `),
            consumeCaptcha: this.database.prepare(`
                UPDATE auth_captchas SET consumed_at = ?
                WHERE challenge_id = ? AND consumed_at IS NULL
            `),
            deleteExpiredSessions: this.database.prepare(`
                DELETE FROM user_sessions WHERE expires_at <= ?
            `),
            deleteExpiredVerifications: this.database.prepare(`
                DELETE FROM email_verification_tokens
                WHERE expires_at <= ? OR consumed_at IS NOT NULL
            `),
            deleteExpiredCaptchas: this.database.prepare(`
                DELETE FROM auth_captchas
                WHERE expires_at <= ? OR consumed_at IS NOT NULL
            `),
            findPendingRoleGrant: this.database.prepare(`
                SELECT email, role, created_at AS createdAt,
                    updated_at AS updatedAt
                FROM pending_role_grants
                WHERE email = ? AND consumed_at IS NULL
            `),
            upsertPendingRoleGrant: this.database.prepare(`
                INSERT INTO pending_role_grants (
                    email, role, created_at, updated_at,
                    consumed_at, consumed_user_uid
                ) VALUES (?, ?, ?, ?, NULL, NULL)
                ON CONFLICT(email) DO UPDATE SET
                    role = excluded.role,
                    updated_at = excluded.updated_at,
                    consumed_at = NULL,
                    consumed_user_uid = NULL
            `),
            consumePendingRoleGrant: this.database.prepare(`
                UPDATE pending_role_grants
                SET consumed_at = ?, consumed_user_uid = ?, updated_at = ?
                WHERE email = ? AND consumed_at IS NULL
            `),
            insertAudit: this.database.prepare(`
                INSERT INTO account_audit_log (
                    actor_type, actor_id, action, target_user_uid,
                    details_json, created_at
                ) VALUES (?, ?, ?, ?, ?, ?)
            `),
            findUserByUid: this.database.prepare(`
                SELECT u.uid, u.display_name AS displayName, u.role,
                    u.avatar_id AS avatarId,
                    u.created_at AS createdAt, u.updated_at AS updatedAt,
                    u.last_login_at AS lastLoginAt, c.email,
                    c.verified_at AS verifiedAt
                FROM users u
                JOIN email_credentials c ON c.user_uid = u.uid
                WHERE u.uid = ?
            `),
            updateUserRole: this.database.prepare(`
                UPDATE users SET role = ?, updated_at = ? WHERE uid = ?
            `),
            updateUserAvatar: this.database.prepare(`
                UPDATE users SET avatar_id = ?, updated_at = ? WHERE uid = ?
            `),
            countAdmins: this.database.prepare(`
                SELECT COUNT(*) AS count FROM users WHERE role = 'admin'
            `),
            listUsers: this.database.prepare(`
                SELECT u.uid, u.display_name AS displayName, u.role,
                    u.created_at AS createdAt, u.updated_at AS updatedAt,
                    u.last_login_at AS lastLoginAt, c.email,
                    c.verified_at AS verifiedAt
                FROM users u
                JOIN email_credentials c ON c.user_uid = u.uid
                WHERE (? = '' OR c.email LIKE ? OR u.display_name LIKE ?)
                ORDER BY u.created_at DESC, u.uid ASC
                LIMIT ? OFFSET ?
            `),
            countUsers: this.database.prepare(`
                SELECT COUNT(*) AS count
                FROM users u
                JOIN email_credentials c ON c.user_uid = u.uid
                WHERE (? = '' OR c.email LIKE ? OR u.display_name LIKE ?)
            `),
            insertServiceToken: this.database.prepare(`
                INSERT INTO service_tokens (
                    id, name, token_hash, scopes_json, created_by_user_uid,
                    created_at, expires_at
                ) VALUES (?, ?, ?, ?, ?, ?, ?)
            `),
            findServiceTokenByHash: this.database.prepare(`
                SELECT id, name, token_hash AS tokenHash,
                    scopes_json AS scopesJson,
                    created_by_user_uid AS createdByUserUid,
                    created_at AS createdAt, expires_at AS expiresAt,
                    revoked_at AS revokedAt, last_used_at AS lastUsedAt
                FROM service_tokens WHERE token_hash = ?
            `),
            listServiceTokens: this.database.prepare(`
                SELECT id, name, scopes_json AS scopesJson,
                    created_by_user_uid AS createdByUserUid,
                    created_at AS createdAt, expires_at AS expiresAt,
                    revoked_at AS revokedAt, last_used_at AS lastUsedAt
                FROM service_tokens
                ORDER BY created_at DESC, id ASC
            `),
            touchServiceToken: this.database.prepare(`
                UPDATE service_tokens SET last_used_at = ? WHERE id = ?
            `),
            revokeServiceToken: this.database.prepare(`
                UPDATE service_tokens SET revoked_at = ?
                WHERE id = ? AND revoked_at IS NULL
            `),
        };

        this.completeRegistrationTransaction = this.database.transaction(input => {
            const now = unixTimeMs(this.now());
            const verification = this.statements.findVerification.get(
                input.tokenHash
            );
            if (!verification || verification.consumedAt !== null ||
                verification.expiresAt <= now) {
                return null;
            }

            this.statements.consumeVerification.run(now, input.tokenHash);
            if (this.statements.findCredentialByEmail.get(verification.email)) {
                return { alreadyRegistered: true };
            }

            const grant = this.statements.findPendingRoleGrant.get(
                verification.email
            );
            const role = grant ? grant.role : 'user';
            const uid = crypto.randomUUID();
            this.statements.insertUser.run(
                uid,
                input.displayName,
                role,
                now,
                now,
                now
            );
            this.statements.insertCredential.run(
                uid,
                verification.email,
                input.passwordHash,
                now,
                now,
                now
            );
            if (grant) {
                this.statements.consumePendingRoleGrant.run(
                    now,
                    uid,
                    now,
                    verification.email
                );
                this.statements.insertAudit.run(
                    'system',
                    'pending-role-grant',
                    'role-grant-consumed',
                    uid,
                    JSON.stringify({ email: verification.email, role }),
                    now
                );
            }
            return {
                uid,
                email: verification.email,
                displayName: input.displayName,
                role,
                avatarId: null,
                verifiedAt: now,
            };
        });

        this.grantRoleTransaction = this.database.transaction(input => {
            const now = unixTimeMs(this.now());
            const credential = this.statements.findCredentialByEmail.get(
                input.email
            );
            if (credential) {
                const previousRole = credential.role;
                this.statements.updateUserRole.run(
                    input.role,
                    now,
                    credential.uid
                );
                this.statements.insertAudit.run(
                    'system',
                    input.actorId,
                    'offline-role-grant',
                    credential.uid,
                    JSON.stringify({
                        email: input.email,
                        previousRole,
                        role: input.role,
                    }),
                    now
                );
                return {
                    status: 'updated',
                    user: this.statements.findUserByUid.get(credential.uid),
                };
            }
            this.statements.upsertPendingRoleGrant.run(
                input.email,
                input.role,
                now,
                now
            );
            this.statements.insertAudit.run(
                'system',
                input.actorId,
                'pending-role-grant-created',
                null,
                JSON.stringify({ email: input.email, role: input.role }),
                now
            );
            return { status: 'pending', email: input.email, role: input.role };
        });

        this.createVerifiedEmailAccountTransaction = this.database.transaction(
            input => {
                const existing = this.statements.findCredentialByEmail.get(
                    input.email
                );
                if (existing) {
                    return { status: 'exists', user: existing };
                }
                const now = unixTimeMs(this.now());
                const uid = crypto.randomUUID();
                this.statements.insertUser.run(
                    uid,
                    input.displayName,
                    input.role,
                    now,
                    now,
                    now
                );
                this.statements.insertCredential.run(
                    uid,
                    input.email,
                    input.passwordHash,
                    now,
                    now,
                    now
                );
                this.statements.consumePendingRoleGrant.run(
                    now,
                    uid,
                    now,
                    input.email
                );
                this.statements.deletePendingVerifications.run(input.email);
                this.statements.insertAudit.run(
                    'system',
                    input.actorId,
                    'offline-verified-account-created',
                    uid,
                    JSON.stringify({
                        email: input.email,
                        role: input.role,
                        emailVerified: true,
                    }),
                    now
                );
                return {
                    status: 'created',
                    user: this.statements.findUserByUid.get(uid),
                };
            }
        );

        this.changeRoleTransaction = this.database.transaction(input => {
            const current = this.statements.findUserByUid.get(input.userUid);
            if (!current) return null;
            if (current.role === input.role) return current;
            if (current.role === 'admin' && input.role === 'user' &&
                this.statements.countAdmins.get().count <= 1) {
                const error = new Error('the last administrator cannot be demoted');
                error.code = 'LAST_ADMIN_REQUIRED';
                throw error;
            }
            const now = unixTimeMs(this.now());
            this.statements.updateUserRole.run(input.role, now, input.userUid);
            this.statements.insertAudit.run(
                'user',
                input.actorUid,
                'user-role-changed',
                input.userUid,
                JSON.stringify({ from: current.role, to: input.role }),
                now
            );
            return this.statements.findUserByUid.get(input.userUid);
        });

        this.changeAvatarTransaction = this.database.transaction(input => {
            const current = this.statements.findUserByUid.get(input.userUid);
            if (!current) return null;
            if (current.avatarId === input.avatarId) return current;
            const now = unixTimeMs(this.now());
            this.statements.updateUserAvatar.run(input.avatarId, now, input.userUid);
            this.statements.insertAudit.run(
                'user',
                input.userUid,
                'user-avatar-changed',
                input.userUid,
                JSON.stringify({ from: current.avatarId, to: input.avatarId }),
                now
            );
            return this.statements.findUserByUid.get(input.userUid);
        });

        this.consumeCaptchaTransaction = this.database.transaction(input => {
            const now = unixTimeMs(this.now());
            const challenge = this.statements.findCaptcha.get(
                input.challengeId
            );
            if (!challenge || challenge.consumedAt !== null ||
                challenge.expiresAt <= now || challenge.action !== input.action) {
                return null;
            }
            this.statements.consumeCaptcha.run(now, input.challengeId);
            return challenge.answerHash;
        });
    }

    hasEmail(email) {
        return Boolean(this.statements.findCredentialByEmail.get(email));
    }

    findCredentialByEmail(email) {
        return this.statements.findCredentialByEmail.get(email) || null;
    }

    grantRoleByEmail({ email, role, actorId = 'offline-bootstrap' }) {
        if (typeof email !== 'string' || !email) {
            throw new TypeError('email is required');
        }
        return this.grantRoleTransaction.immediate({
            email,
            role: normalizeRole(role),
            actorId: String(actorId || 'offline-bootstrap'),
        });
    }

    findPendingRoleGrant(email) {
        return this.statements.findPendingRoleGrant.get(email) || null;
    }

    createVerifiedEmailAccount({
        email,
        passwordHash,
        displayName,
        role,
        actorId = 'offline-account-create',
    }) {
        if (typeof email !== 'string' || !email ||
            typeof passwordHash !== 'string' || !passwordHash ||
            typeof displayName !== 'string' || !displayName) {
            throw new TypeError(
                'email, passwordHash and displayName are required'
            );
        }
        return this.createVerifiedEmailAccountTransaction.immediate({
            email,
            passwordHash,
            displayName: displayName.slice(0, 64),
            role: normalizeRole(role),
            actorId: String(actorId || 'offline-account-create'),
        });
    }

    createEmailVerification({ tokenHash, email, locale, ttlMs }) {
        const now = unixTimeMs(this.now());
        this.database.transaction(() => {
            this.statements.deletePendingVerifications.run(email);
            this.statements.insertVerification.run(
                tokenHash,
                email,
                locale,
                now,
                now + ttlMs
            );
        })();
        return { expiresAt: now + ttlMs };
    }

    deleteEmailVerification(tokenHash) {
        this.statements.deleteVerification.run(tokenHash);
    }

    findPendingEmailVerification(tokenHash) {
        const record = this.statements.findVerification.get(tokenHash);
        if (!record || record.consumedAt !== null ||
            record.expiresAt <= unixTimeMs(this.now())) {
            return null;
        }
        return record;
    }

    completeRegistration(input) {
        return this.completeRegistrationTransaction.immediate(input);
    }

    recordLogin(userUid) {
        const now = unixTimeMs(this.now());
        this.statements.updateLastLogin.run(now, now, userUid);
    }

    createSession({ tokenHash, userUid, ttlMs }) {
        const now = unixTimeMs(this.now());
        const expiresAt = now + ttlMs;
        this.statements.insertSession.run(tokenHash, userUid, now, expiresAt);
        return { issuedAt: now, expiresAt };
    }

    findSession(tokenHash) {
        const session = this.statements.findSession.get(tokenHash);
        if (!session) {
            return null;
        }
        if (session.expiresAt <= unixTimeMs(this.now())) {
            this.statements.revokeSession.run(tokenHash);
            return null;
        }
        return session;
    }

    revokeSession(tokenHash) {
        this.statements.revokeSession.run(tokenHash);
    }

    listUsers({ query = '', limit = 50, offset = 0 } = {}) {
        const normalizedQuery = String(query || '').trim().slice(0, 254);
        const pattern = normalizedQuery ? `%${normalizedQuery}%` : '';
        const normalizedLimit = Math.max(1, Math.min(100, Math.trunc(limit)));
        const normalizedOffset = Math.max(0, Math.trunc(offset));
        const users = this.statements.listUsers.all(
            normalizedQuery,
            pattern,
            pattern,
            normalizedLimit,
            normalizedOffset
        );
        const total = this.statements.countUsers.get(
            normalizedQuery,
            pattern,
            pattern
        ).count;
        return { users, total, limit: normalizedLimit, offset: normalizedOffset };
    }

    changeUserRole({ userUid, role, actorUid }) {
        if (typeof userUid !== 'string' || typeof actorUid !== 'string') {
            throw new TypeError('userUid and actorUid are required');
        }
        return this.changeRoleTransaction.immediate({
            userUid,
            role: normalizeRole(role),
            actorUid,
        });
    }

    changeUserAvatar({ userUid, avatarId }) {
        if (typeof userUid !== 'string' || !userUid) {
            throw new TypeError('userUid is required');
        }
        return this.changeAvatarTransaction.immediate({ userUid, avatarId });
    }

    createServiceToken({
        id,
        name,
        tokenHash,
        scopes,
        createdByUserUid,
        expiresAt,
    }) {
        const now = unixTimeMs(this.now());
        this.statements.insertServiceToken.run(
            id,
            name,
            tokenHash,
            JSON.stringify(scopes),
            createdByUserUid,
            now,
            unixTimeMs(expiresAt)
        );
        this.statements.insertAudit.run(
            'user',
            createdByUserUid,
            'service-token-created',
            createdByUserUid,
            JSON.stringify({ id, name, scopes, expiresAt }),
            now
        );
        return this.findServiceTokenByHash(tokenHash);
    }

    findServiceTokenByHash(tokenHash) {
        const record = this.statements.findServiceTokenByHash.get(tokenHash);
        if (!record) return null;
        return { ...record, scopes: JSON.parse(record.scopesJson) };
    }

    recordServiceTokenUse(id) {
        this.statements.touchServiceToken.run(unixTimeMs(this.now()), id);
    }

    listServiceTokens() {
        return this.statements.listServiceTokens.all().map(record => ({
            ...record,
            scopes: JSON.parse(record.scopesJson),
            scopesJson: undefined,
        }));
    }

    revokeServiceToken({ id, actorUid }) {
        const now = unixTimeMs(this.now());
        const result = this.statements.revokeServiceToken.run(now, id);
        if (result.changes > 0) {
            this.statements.insertAudit.run(
                'user',
                actorUid,
                'service-token-revoked',
                actorUid,
                JSON.stringify({ id }),
                now
            );
        }
        return result.changes > 0;
    }

    createCaptcha({ challengeId, action, answerHash, ttlMs }) {
        const now = unixTimeMs(this.now());
        const expiresAt = now + ttlMs;
        this.statements.insertCaptcha.run(
            challengeId,
            action,
            answerHash,
            now,
            expiresAt
        );
        return { expiresAt };
    }

    consumeCaptcha(input) {
        return this.consumeCaptchaTransaction.immediate(input);
    }

    cleanupExpired() {
        const now = unixTimeMs(this.now());
        return this.database.transaction(() => ({
            sessions: this.statements.deleteExpiredSessions.run(now).changes,
            verifications:
                this.statements.deleteExpiredVerifications.run(now).changes,
            captchas: this.statements.deleteExpiredCaptchas.run(now).changes,
        }))();
    }

    close() {
        this.database.close();
    }
}

module.exports = {
    ACCOUNT_ROLES,
    DATABASE_VERSION,
    UserAccountStore,
    normalizeRole,
};
