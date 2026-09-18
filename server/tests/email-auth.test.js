'use strict';

const assert = require('node:assert/strict');
const fs = require('fs-extra');
const http = require('node:http');
const os = require('node:os');
const path = require('node:path');
const test = require('node:test');
const express = require('express');

const {
    EmailAuthError,
    EmailAuthService,
    captchaAnswerHash,
    createEmailAuthFromEnvironment,
    hashToken,
    initEmailAuthRoutes,
    normalizeEmail,
    renderCaptchaSvg,
    sessionCookie,
} = require('../src/email-auth');
const { UserAccountStore } = require('../src/user-account-store');

function fixture() {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'st-dash-email-auth-'));
    const clock = { value: 1800000000000 };
    const messages = [];
    const store = new UserAccountStore({
        databasePath: path.join(root, 'user_accounts.sqlite3'),
        now: () => clock.value,
    });
    const service = new EmailAuthService({
        store,
        enabled: true,
        publicOrigin: 'https://st-dash.com',
        allowedOrigins: ['https://st-dash.com'],
        now: () => clock.value,
        mailer: {
            async sendVerification(message) {
                messages.push(message);
            },
        },
    });
    return { root, clock, messages, store, service };
}

function addCaptcha(value, action, answer = '234567') {
    const challengeId = cryptoRandomUuidForTest(value.store);
    value.store.createCaptcha({
        challengeId,
        action,
        answerHash: captchaAnswerHash(challengeId, answer),
        ttlMs: 300000,
    });
    return { challengeId, answer };
}

let testUuidCounter = 1;
function cryptoRandomUuidForTest() {
    const suffix = String(testUuidCounter++).padStart(12, '0');
    return `10000000-0000-4000-8000-${suffix}`;
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
                headers: response.headers,
                body: Buffer.concat(chunks).toString('utf8'),
            }));
        });
        req.on('error', reject);
        if (body) req.write(body);
        req.end();
    });
}

test('registration verifies captcha and email before creating a UUID user session', async t => {
    const value = fixture();
    t.after(() => {
        value.store.close();
        fs.removeSync(value.root);
    });

    const captcha = addCaptcha(value, 'register');
    const accepted = await value.service.requestRegistration({
        email: ' Person@ST-DASH.com ',
        locale: 'zh',
        captchaChallengeId: captcha.challengeId,
        captchaAnswer: captcha.answer,
    }, '127.0.0.1');
    assert.deepEqual(accepted, { accepted: true });
    assert.equal(value.messages.length, 1);
    assert.equal(value.messages[0].email, 'person@st-dash.com');
    assert.equal(value.messages[0].locale, 'zh');
    assert.equal(
        value.store.database.prepare(`
            SELECT count(*) AS count FROM email_verification_tokens
            WHERE token_hash = ?
        `).get(value.messages[0].token).count,
        0,
        'raw verification tokens must not be persisted'
    );

    const session = await value.service.completeRegistration({
        token: value.messages[0].token,
        password: 'correct horse battery staple',
    });
    assert.match(
        session.user.uid,
        /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/
    );
    assert.equal(session.user.email, 'person@st-dash.com');
    assert.equal(value.service.resolveSession(session.token).uid, session.user.uid);
    assert.equal(
        value.store.database.prepare(`
            SELECT count(*) AS count FROM user_sessions WHERE token_hash = ?
        `).get(session.token).count,
        0,
        'raw session tokens must not be persisted'
    );
    assert.equal(
        value.store.database.prepare(`
            SELECT count(*) AS count FROM user_sessions WHERE token_hash = ?
        `).get(hashToken(session.token)).count,
        1
    );
    await assert.rejects(
        value.service.completeRegistration({
            token: value.messages[0].token,
            password: 'another secure password',
        }),
        error => error.code === 'VERIFICATION_LINK_INVALID'
    );
});

test('email/password login requires a fresh one-time captcha', async t => {
    const value = fixture();
    t.after(() => {
        value.store.close();
        fs.removeSync(value.root);
    });

    const registrationCaptcha = addCaptcha(value, 'register');
    await value.service.requestRegistration({
        email: 'login@example.com',
        locale: 'en',
        captchaChallengeId: registrationCaptcha.challengeId,
        captchaAnswer: registrationCaptcha.answer,
    }, 'local');
    await value.service.completeRegistration({
        token: value.messages[0].token,
        password: 'a very secure password',
    });

    const wrongCaptcha = addCaptcha(value, 'login');
    await assert.rejects(
        value.service.login({
            email: 'login@example.com',
            password: 'a very secure password',
            captchaChallengeId: wrongCaptcha.challengeId,
            captchaAnswer: '876543',
        }, 'local'),
        error => error.code === 'CAPTCHA_INVALID'
    );
    await assert.rejects(
        value.service.login({
            email: 'login@example.com',
            password: 'a very secure password',
            captchaChallengeId: wrongCaptcha.challengeId,
            captchaAnswer: wrongCaptcha.answer,
        }, 'local'),
        error => error.code === 'CAPTCHA_INVALID',
        'a failed captcha attempt consumes the challenge'
    );

    const loginCaptcha = addCaptcha(value, 'login', '876543');
    const session = await value.service.login({
        email: 'LOGIN@example.com',
        password: 'a very secure password',
        captchaChallengeId: loginCaptcha.challengeId,
        captchaAnswer: loginCaptcha.answer,
    }, 'local');
    assert.equal(session.user.email, 'login@example.com');
});

test('validation, cookies and captcha rendering expose no raw answer', () => {
    assert.equal(normalizeEmail(' Test@Example.COM '), 'test@example.com');
    assert.throws(
        () => normalizeEmail('not-an-email'),
        error => error instanceof EmailAuthError && error.code === 'INVALID_EMAIL'
    );
    const image = renderCaptchaSvg('234567');
    assert.match(image, /^data:image\/svg\+xml;base64,/);
    const svg = Buffer.from(image.split(',')[1], 'base64').toString('utf8');
    assert.doesNotMatch(svg, /234567/);
    assert.match(
        sessionCookie('__Host-st-dash-user', 'secret', true, 604800),
        /^__Host-st-dash-user=secret; Path=\/; HttpOnly; SameSite=Lax; Max-Age=604800; Secure$/
    );
});

test('HTTP auth routes enforce exact origin and revoke the current cookie session', async t => {
    const value = fixture();
    t.after(() => {
        value.store.close();
        fs.removeSync(value.root);
    });
    const app = express();
    app.use(express.json());
    initEmailAuthRoutes(app, value.service);
    const server = app.listen(0, '127.0.0.1');
    await new Promise(resolve => server.once('listening', resolve));
    t.after(() => new Promise(resolve => server.close(resolve)));

    const rejected = await request(server, 'POST', '/api/auth/captcha', {
        headers: { Origin: 'https://attacker.example' },
        body: { action: 'login' },
    });
    assert.equal(rejected.status, 403);
    assert.equal(JSON.parse(rejected.body).error, 'ORIGIN_NOT_ALLOWED');

    const accepted = await request(server, 'POST', '/api/auth/captcha', {
        headers: { Origin: 'https://st-dash.com' },
        body: { action: 'login' },
    });
    assert.equal(accepted.status, 201);
    assert.match(JSON.parse(accepted.body).image, /^data:image\/svg\+xml;base64,/);

    value.store.createEmailVerification({
        tokenHash: 'f'.repeat(64),
        email: 'session@example.com',
        locale: 'en',
        ttlMs: 1000,
    });
    const user = value.store.completeRegistration({
        tokenHash: 'f'.repeat(64),
        passwordHash: '$argon2id$test',
        displayName: 'session',
    });
    const session = value.service.issueSession(user);
    const cookie = `__Host-st-dash-user=${session.token}`;
    const resolved = await request(server, 'GET', '/api/auth/session', {
        headers: { Cookie: cookie },
    });
    assert.equal(resolved.status, 200);
    assert.equal(JSON.parse(resolved.body).user.uid, user.uid);
    assert.equal(JSON.parse(resolved.body).user.avatarId, null);

    const avatar = await request(server, 'PATCH', '/api/auth/profile/avatar', {
        headers: {
            Origin: 'https://st-dash.com',
            Cookie: cookie,
        },
        body: { avatarId: 'sf6-chun-li' },
    });
    assert.equal(avatar.status, 200);
    assert.equal(JSON.parse(avatar.body).user.avatarId, 'sf6-chun-li');
    assert.equal(
        JSON.parse(avatar.body).user.avatarUrl,
        '/images/account-avatars/sf6-chun-li.webp'
    );

    const invalidAvatar = await request(
        server,
        'PATCH',
        '/api/auth/profile/avatar',
        {
            headers: {
                Origin: 'https://st-dash.com',
                Cookie: cookie,
            },
            body: { avatarId: '../../secrets' },
        }
    );
    assert.equal(invalidAvatar.status, 400);
    assert.equal(JSON.parse(invalidAvatar.body).error, 'INVALID_AVATAR');

    const logout = await request(server, 'POST', '/api/auth/logout', {
        headers: {
            Origin: 'https://st-dash.com',
            Cookie: cookie,
        },
    });
    assert.equal(logout.status, 204);
    assert.match(logout.headers['set-cookie'][0], /Max-Age=0/);
    assert.equal(value.service.resolveSession(session.token), null);
});

test('production email auth requires an exact deployed origin and file-backed Resend key', t => {
    const value = fixture();
    t.after(() => {
        value.store.close();
        fs.removeSync(value.root);
    });
    assert.equal(createEmailAuthFromEnvironment({
        store: value.store,
        allowedOrigins: ['https://firmware.st-dash.com'],
        environment: {},
    }).enabled, false);
    assert.throws(() => createEmailAuthFromEnvironment({
        store: value.store,
        allowedOrigins: ['https://firmware.st-dash.com'],
        environment: {
            NODE_ENV: 'production',
            USER_AUTH_ENABLED: '1',
            USER_AUTH_PUBLIC_ORIGIN: 'https://firmware.st-dash.com',
            RESEND_API_KEY: 'inline-secret',
        },
    }), /RESEND_API_KEY_FILE is required instead/);
    const secretFile = path.join(value.root, 'resend-api-key');
    fs.writeFileSync(secretFile, 're_test_only\n');
    const configured = createEmailAuthFromEnvironment({
        store: value.store,
        allowedOrigins: ['https://firmware.st-dash.com'],
        environment: {
            NODE_ENV: 'production',
            USER_AUTH_ENABLED: '1',
            USER_AUTH_PUBLIC_ORIGIN: 'https://firmware.st-dash.com',
            USER_AUTH_EMAIL_FROM:
                'ST-Dash <no-reply@auth.st-dash.com>',
            RESEND_API_KEY_FILE: secretFile,
        },
    });
    assert.equal(configured.enabled, true);
    assert.equal(configured.cookieName, '__Host-st-dash-user');
});

test('loopback development can preview verification without a mail provider', async t => {
    const value = fixture();
    t.after(() => {
        value.store.close();
        fs.removeSync(value.root);
    });
    const service = createEmailAuthFromEnvironment({
        store: value.store,
        allowedOrigins: ['http://localhost:3001'],
        environment: {
            NODE_ENV: 'development',
            USER_AUTH_ENABLED: '1',
            USER_AUTH_LOCAL_PREVIEW: '1',
            USER_AUTH_PUBLIC_ORIGIN: 'http://localhost:3001',
        },
    });
    const captcha = addCaptcha(value, 'register');
    const result = await service.requestRegistration({
        email: 'local@example.com',
        locale: 'en',
        captchaChallengeId: captcha.challengeId,
        captchaAnswer: captcha.answer,
    }, '127.0.0.1');

    assert.equal(service.enabled, true);
    assert.match(result.verificationToken, /^[A-Za-z0-9_-]{43}$/);
    const session = await service.completeRegistration({
        token: result.verificationToken,
        password: 'local preview password',
    });
    assert.equal(session.user.email, 'local@example.com');

    assert.throws(() => createEmailAuthFromEnvironment({
        store: value.store,
        allowedOrigins: ['https://st-dash.com'],
        environment: {
            NODE_ENV: 'development',
            USER_AUTH_ENABLED: '1',
            USER_AUTH_LOCAL_PREVIEW: '1',
            USER_AUTH_PUBLIC_ORIGIN: 'https://st-dash.com',
        },
    }), /restricted to loopback development/);
});
