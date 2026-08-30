'use strict';

const assert = require('node:assert/strict');
const fs = require('fs-extra');
const http = require('node:http');
const os = require('node:os');
const path = require('node:path');
const test = require('node:test');
const express = require('express');

const {
    AdminAccessService,
    SERVICE_TOKEN_PATTERN,
    initAdminRoutes,
} = require('../src/admin-access');
const { EmailAuthService } = require('../src/email-auth');
const { UserAccountStore } = require('../src/user-account-store');

function createAccount(store, email, role, tokenCharacter) {
    if (role === 'admin') {
        store.grantRoleByEmail({ email, role });
    }
    const tokenHash = tokenCharacter.repeat(64);
    store.createEmailVerification({
        tokenHash,
        email,
        locale: 'en',
        ttlMs: 60000,
    });
    return store.completeRegistration({
        tokenHash,
        passwordHash: '$argon2id$fixture',
        displayName: email.split('@')[0],
    });
}

function request(server, method, requestPath, options = {}) {
    return new Promise((resolve, reject) => {
        const body = options.body === undefined
            ? null
            : JSON.stringify(options.body);
        const address = server.address();
        const req = http.request({
            host: '127.0.0.1',
            port: address.port,
            method,
            path: requestPath,
            headers: {
                ...(body ? {
                    'Content-Type': 'application/json',
                    'Content-Length': Buffer.byteLength(body),
                } : {}),
                ...options.headers,
            },
        }, response => {
            const chunks = [];
            response.on('data', chunk => chunks.push(chunk));
            response.on('end', () => resolve({
                status: response.statusCode,
                body: Buffer.concat(chunks).toString('utf8'),
            }));
        });
        req.on('error', reject);
        if (body) req.write(body);
        req.end();
    });
}

test('email admins manage roles and scoped service tokens replace Basic auth', async t => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'st-dash-admin-'));
    const store = new UserAccountStore({
        databasePath: path.join(root, 'user_accounts.sqlite3'),
    });
    const emailAuth = new EmailAuthService({
        store,
        enabled: true,
        publicOrigin: 'http://localhost:3000',
        allowedOrigins: ['http://localhost:3000'],
        mailer: { async sendVerification() {} },
    });
    const admin = createAccount(
        store,
        'admin@example.com',
        'admin',
        'a'
    );
    const user = createAccount(store, 'user@example.com', 'user', 'b');
    const adminSession = emailAuth.issueSession(admin);
    const userSession = emailAuth.issueSession(user);
    const service = new AdminAccessService({ store, emailAuth });
    const app = express();
    app.use(express.json());
    initAdminRoutes(app, service);
    app.post(
        '/api/protected/device',
        service.requireAdmin({ serviceScope: 'device.manage' }),
        (req, res) => res.json({ actor: req.authenticatedAdmin.actorType })
    );
    app.post(
        '/api/protected/firmware',
        service.requireAdmin({ serviceScope: 'firmware.manage' }),
        (req, res) => res.json({ actor: req.authenticatedAdmin.actorType })
    );
    app.post(
        '/api/protected/device-plus-human',
        service.requireAdmin({ humanOnly: true, allowDeviceBearer: true }),
        (req, res) => res.json({ actor: req.authenticatedAdmin.actorType })
    );
    const server = app.listen(0, '127.0.0.1');
    await new Promise(resolve => server.once('listening', resolve));
    t.after(() => new Promise(resolve => server.close(resolve)));
    t.after(() => {
        store.close();
        fs.removeSync(root);
    });

    const adminCookie = `st_dash_user=${adminSession.token}`;
    const userCookie = `st_dash_user=${userSession.token}`;
    const denied = await request(server, 'GET', '/api/admin/users', {
        headers: { Cookie: userCookie },
    });
    assert.equal(denied.status, 403);
    const users = await request(server, 'GET', '/api/admin/users', {
        headers: { Cookie: adminCookie },
    });
    assert.equal(users.status, 200);
    assert.equal(JSON.parse(users.body).data.total, 2);

    const promoted = await request(
        server,
        'PATCH',
        `/api/admin/users/${user.uid}/role`,
        {
            headers: {
                Cookie: adminCookie,
                Origin: 'http://localhost:3000',
            },
            body: { role: 'admin' },
        }
    );
    assert.equal(promoted.status, 200);
    assert.equal(JSON.parse(promoted.body).data.user.role, 'admin');
    const promotedSessionAccess = await request(
        server,
        'GET',
        '/api/admin/users',
        { headers: { Cookie: userCookie } }
    );
    assert.equal(promotedSessionAccess.status, 200);
    const demoted = await request(
        server,
        'PATCH',
        `/api/admin/users/${user.uid}/role`,
        {
            headers: {
                Cookie: adminCookie,
                Origin: 'http://localhost:3000',
            },
            body: { role: 'user' },
        }
    );
    assert.equal(demoted.status, 200);
    const demotedSessionAccess = await request(
        server,
        'GET',
        '/api/admin/users',
        { headers: { Cookie: userCookie } }
    );
    assert.equal(demotedSessionAccess.status, 403);

    const created = await request(server, 'POST', '/api/admin/service-tokens', {
        headers: {
            Cookie: adminCookie,
            Origin: 'http://localhost:3000',
        },
        body: {
            name: 'device automation',
            scopes: ['device.manage'],
            expiresInDays: 30,
        },
    });
    assert.equal(created.status, 201);
    const createdData = JSON.parse(created.body).data;
    assert.match(createdData.secret, SERVICE_TOKEN_PATTERN);
    assert.equal(
        store.database.prepare(
            'SELECT COUNT(*) AS count FROM service_tokens WHERE token_hash = ?'
        ).get(createdData.secret).count,
        0
    );

    const basic = await request(server, 'POST', '/api/protected/device', {
        headers: { Authorization: 'Basic YWRtaW46YWRtaW4xMjM=' },
    });
    assert.equal(basic.status, 401);
    assert.equal(JSON.parse(basic.body).error, 'BASIC_AUTH_RETIRED');
    const device = await request(server, 'POST', '/api/protected/device', {
        headers: { Authorization: `Bearer ${createdData.secret}` },
    });
    assert.equal(device.status, 200);
    const firmware = await request(server, 'POST', '/api/protected/firmware', {
        headers: { Authorization: `Bearer ${createdData.secret}` },
    });
    assert.equal(firmware.status, 403);

    const humanOnly = await request(server, 'GET', '/api/admin/users', {
        headers: { Authorization: `Bearer ${createdData.secret}` },
    });
    assert.equal(humanOnly.status, 403);
    const devicePlusHuman = await request(
        server,
        'POST',
        '/api/protected/device-plus-human',
        {
            headers: {
                Authorization: 'Bearer validated-by-device-middleware',
                Cookie: adminCookie,
                Origin: 'http://localhost:3000',
            },
        }
    );
    assert.equal(devicePlusHuman.status, 200);
    assert.equal(JSON.parse(devicePlusHuman.body).actor, 'user');
    const listed = await request(server, 'GET', '/api/admin/service-tokens', {
        headers: { Cookie: adminCookie },
    });
    assert.equal(listed.status, 200);
    assert.doesNotMatch(listed.body, new RegExp(createdData.secret));
    assert.deepEqual(
        Object.keys(JSON.parse(listed.body).data.tokens[0]).sort(),
        ['expiresAt', 'id', 'name', 'revokedAt', 'scopes']
    );

    const revoked = await request(
        server,
        'DELETE',
        `/api/admin/service-tokens/${createdData.token.id}`,
        {
            headers: {
                Cookie: adminCookie,
                Origin: 'http://localhost:3000',
            },
        }
    );
    assert.equal(revoked.status, 204);
    const afterRevoke = await request(server, 'POST', '/api/protected/device', {
        headers: { Authorization: `Bearer ${createdData.secret}` },
    });
    assert.equal(afterRevoke.status, 401);
});
