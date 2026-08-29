'use strict';

const crypto = require('crypto');
const fs = require('fs-extra');
const path = require('path');
const fetch = require('node-fetch');
const {
    Algorithm,
    hash: argon2Hash,
    verify: argon2Verify,
} = require('@node-rs/argon2');
const { avatarUrl, normalizeAvatarId } = require('./account-avatars');

const SESSION_TTL_MS = 7 * 24 * 60 * 60 * 1000;
const VERIFICATION_TTL_MS = 30 * 60 * 1000;
const CAPTCHA_TTL_MS = 5 * 60 * 1000;
const PASSWORD_MIN_LENGTH = 10;
const PASSWORD_MAX_LENGTH = 128;
const EMAIL_PATTERN = /^[^\s@]+@[^\s@]+\.[^\s@]+$/;
const TOKEN_PATTERN = /^[A-Za-z0-9_-]{43}$/;
const CAPTCHA_ID_PATTERN = /^[0-9a-f]{8}-[0-9a-f-]{27}$/;

class EmailAuthError extends Error {
    constructor(code, message, status = 400) {
        super(message);
        this.name = 'EmailAuthError';
        this.code = code;
        this.status = status;
    }
}

class SlidingWindowRateLimiter {
    constructor(options = {}) {
        this.limit = options.limit || 10;
        this.windowMs = options.windowMs || 15 * 60 * 1000;
        this.maxKeys = options.maxKeys || 10000;
        this.now = options.now || Date.now;
        this.attempts = new Map();
        this.operations = 0;
    }

    check(key) {
        const normalizedKey = String(key || 'unknown').slice(0, 320);
        const now = this.now();
        const cutoff = now - this.windowMs;
        const active = (this.attempts.get(normalizedKey) || [])
            .filter(timestamp => timestamp > cutoff);
        if (active.length >= this.limit) {
            throw new EmailAuthError(
                'AUTH_RATE_LIMITED',
                'Too many attempts. Please try again later.',
                429
            );
        }
        if (!this.attempts.has(normalizedKey) &&
            this.attempts.size >= this.maxKeys) {
            throw new EmailAuthError(
                'AUTH_TEMPORARILY_UNAVAILABLE',
                'Authentication is temporarily unavailable.',
                503
            );
        }
        active.push(now);
        this.attempts.set(normalizedKey, active);
        this.operations += 1;
        if ((this.operations & 0xff) === 0) {
            for (const [entryKey, timestamps] of this.attempts.entries()) {
                const remaining = timestamps.filter(value => value > cutoff);
                if (remaining.length === 0) {
                    this.attempts.delete(entryKey);
                } else {
                    this.attempts.set(entryKey, remaining);
                }
            }
        }
    }
}

function normalizeEmail(value) {
    if (typeof value !== 'string') {
        throw new EmailAuthError('INVALID_EMAIL', 'Enter a valid email address.');
    }
    const email = value.trim().toLowerCase();
    if (email.length < 3 || email.length > 254 ||
        !EMAIL_PATTERN.test(email) || email.includes('..')) {
        throw new EmailAuthError('INVALID_EMAIL', 'Enter a valid email address.');
    }
    return email;
}

function validatePassword(value) {
    if (typeof value !== 'string' ||
        value.length < PASSWORD_MIN_LENGTH ||
        value.length > PASSWORD_MAX_LENGTH) {
        throw new EmailAuthError(
            'INVALID_PASSWORD',
            `Password must be ${PASSWORD_MIN_LENGTH}-${PASSWORD_MAX_LENGTH} characters.`
        );
    }
    return value;
}

function normalizeLocale(value) {
    return value === 'zh' ? 'zh' : 'en';
}

function hashToken(value) {
    return crypto.createHash('sha256').update(value).digest('hex');
}

function randomToken() {
    return crypto.randomBytes(32).toString('base64url');
}

function deriveDisplayName(email) {
    return email.slice(0, email.indexOf('@')).slice(0, 64) || 'User';
}

function captchaAnswerHash(challengeId, answer) {
    return hashToken(`${challengeId}:${String(answer).trim()}`);
}

function timingSafeEqualHex(left, right) {
    if (typeof left !== 'string' || typeof right !== 'string' ||
        left.length !== right.length || !/^[0-9a-f]+$/i.test(left + right)) {
        return false;
    }
    return crypto.timingSafeEqual(Buffer.from(left, 'hex'), Buffer.from(right, 'hex'));
}

const DIGIT_GLYPHS = {
    2: ['11110', '00001', '00001', '11110', '10000', '10000', '11111'],
    3: ['11110', '00001', '00001', '01110', '00001', '00001', '11110'],
    4: ['10010', '10010', '10010', '11111', '00010', '00010', '00010'],
    5: ['11111', '10000', '10000', '11110', '00001', '00001', '11110'],
    6: ['01111', '10000', '10000', '11110', '10001', '10001', '01110'],
    7: ['11111', '00001', '00010', '00100', '01000', '01000', '01000'],
    8: ['01110', '10001', '10001', '01110', '10001', '10001', '01110'],
    9: ['01110', '10001', '10001', '01111', '00001', '00001', '11110'],
};

function randomInt(maximum) {
    return crypto.randomInt(0, maximum);
}

function createCaptchaAnswer() {
    const digits = '23456789';
    return Array.from({ length: 6 }, () => digits[randomInt(digits.length)]).join('');
}

function renderCaptchaSvg(answer) {
    const width = 240;
    const height = 72;
    const glyphs = [...answer].map((digit, index) => {
        const rows = DIGIT_GLYPHS[digit];
        const cell = 5;
        const blocks = [];
        for (let row = 0; row < rows.length; row += 1) {
            for (let column = 0; column < rows[row].length; column += 1) {
                if (rows[row][column] === '1') {
                    blocks.push(
                        `M${column * cell},${row * cell}h${cell - 1}v${cell - 1}h-${cell - 1}z`
                    );
                }
            }
        }
        const x = 17 + index * 36 + randomInt(5);
        const y = 16 + randomInt(7);
        const rotation = randomInt(13) - 6;
        const color = ['#276749', '#2b6cb0', '#6b46c1', '#9c4221'][randomInt(4)];
        return `<path d="${blocks.join('')}" fill="${color}" transform="translate(${x} ${y}) rotate(${rotation} 12 17)"/>`;
    }).join('');
    const lines = Array.from({ length: 8 }, () => {
        const x1 = randomInt(width);
        const y1 = randomInt(height);
        const x2 = randomInt(width);
        const y2 = randomInt(height);
        return `<line x1="${x1}" y1="${y1}" x2="${x2}" y2="${y2}" stroke="#718096" stroke-opacity="0.38" stroke-width="${1 + randomInt(2)}"/>`;
    }).join('');
    const dots = Array.from({ length: 40 }, () => (
        `<circle cx="${randomInt(width)}" cy="${randomInt(height)}" r="${1 + randomInt(2)}" fill="#4a5568" fill-opacity="0.32"/>`
    )).join('');
    const svg = `<svg xmlns="http://www.w3.org/2000/svg" width="${width}" height="${height}" viewBox="0 0 ${width} ${height}" role="img"><rect width="240" height="72" rx="8" fill="#edf2f7"/>${dots}${lines}${glyphs}</svg>`;
    return `data:image/svg+xml;base64,${Buffer.from(svg).toString('base64')}`;
}

function escapeHtml(value) {
    return String(value)
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;')
        .replace(/'/g, '&#39;');
}

class ResendMailer {
    constructor(options) {
        this.apiKey = options.apiKey;
        this.from = options.from;
        this.publicOrigin = options.publicOrigin;
        this.fetch = options.fetch || fetch;
    }

    async sendVerification({ email, token, locale }) {
        const chinese = locale === 'zh';
        const url = `${this.publicOrigin}/auth/verify/?token=${encodeURIComponent(token)}&lang=${chinese ? 'zh' : 'en'}`;
        const subject = chinese ? '验证你的邮箱地址' : 'Verify your email address';
        const intro = chinese
            ? '点击下面的按钮验证邮箱并设置密码。此链接将在 30 分钟后失效。'
            : 'Verify your email and set a password. This link expires in 30 minutes.';
        const button = chinese ? '验证邮箱' : 'Verify email';
        const response = await this.fetch('https://api.resend.com/emails', {
            method: 'POST',
            headers: {
                Authorization: `Bearer ${this.apiKey}`,
                'Content-Type': 'application/json',
            },
            body: JSON.stringify({
                from: this.from,
                to: [email],
                subject,
                text: `${intro}\n\n${url}`,
                html: `<div><p>${escapeHtml(intro)}</p><p><a href="${escapeHtml(url)}">${escapeHtml(button)}</a></p><p>${escapeHtml(url)}</p></div>`,
            }),
        });
        if (!response.ok) {
            throw new EmailAuthError(
                'EMAIL_DELIVERY_FAILED',
                'Verification email could not be sent.',
                502
            );
        }
    }
}

class LocalPreviewMailer {
    async sendVerification() {
        // The loopback-only development flow returns the one-time token to
        // the same browser instead of sending or logging an email.
    }
}

function parseCookies(header) {
    const cookies = {};
    for (const entry of String(header || '').split(';')) {
        const separator = entry.indexOf('=');
        if (separator <= 0) continue;
        const name = entry.slice(0, separator).trim();
        const value = entry.slice(separator + 1).trim();
        try {
            cookies[name] = decodeURIComponent(value);
        } catch (error) {
            cookies[name] = '';
        }
    }
    return cookies;
}

function sessionCookie(name, token, secure, maxAgeSeconds) {
    const parts = [
        `${name}=${encodeURIComponent(token)}`,
        'Path=/',
        'HttpOnly',
        'SameSite=Lax',
        `Max-Age=${Math.max(0, Math.trunc(maxAgeSeconds))}`,
    ];
    if (secure) parts.push('Secure');
    return parts.join('; ');
}

class EmailAuthService {
    constructor(options) {
        this.store = options.store;
        this.enabled = Boolean(options.enabled);
        this.mailer = options.mailer || null;
        this.allowedOrigins = new Set(options.allowedOrigins || []);
        this.publicOrigin = options.publicOrigin || null;
        this.exposeVerificationToken = Boolean(
            options.exposeVerificationToken
        );
        this.secureCookie = Boolean(
            this.publicOrigin && this.publicOrigin.startsWith('https://')
        );
        this.cookieName = this.secureCookie
            ? '__Host-st-dash-user'
            : 'st_dash_user';
        this.now = options.now || Date.now;
        this.captchaLimiter = options.captchaLimiter ||
            new SlidingWindowRateLimiter({ limit: 20, windowMs: 5 * 60 * 1000 });
        this.registrationLimiter = options.registrationLimiter ||
            new SlidingWindowRateLimiter({ limit: 5, windowMs: 60 * 60 * 1000 });
        this.loginLimiter = options.loginLimiter ||
            new SlidingWindowRateLimiter({ limit: 10, windowMs: 15 * 60 * 1000 });
        this.completionLimiter = options.completionLimiter ||
            new SlidingWindowRateLimiter({ limit: 10, windowMs: 15 * 60 * 1000 });
        this.dummyHashPromise = null;
    }

    requireEnabled() {
        if (!this.enabled || !this.mailer) {
            throw new EmailAuthError(
                'EMAIL_AUTH_NOT_CONFIGURED',
                'Email sign-in is not configured on this server.',
                503
            );
        }
    }

    requireOrigin(origin) {
        if (!origin || !this.allowedOrigins.has(origin) ||
            (this.publicOrigin && origin !== this.publicOrigin)) {
            throw new EmailAuthError(
                'ORIGIN_NOT_ALLOWED',
                'The request origin is not allowed.',
                403
            );
        }
    }

    createCaptcha(action, remoteKey) {
        this.requireEnabled();
        if (action !== 'register' && action !== 'login') {
            throw new EmailAuthError('INVALID_CAPTCHA_ACTION', 'Invalid captcha action.');
        }
        this.captchaLimiter.check(remoteKey);
        const challengeId = crypto.randomUUID();
        const answer = createCaptchaAnswer();
        const { expiresAt } = this.store.createCaptcha({
            challengeId,
            action,
            answerHash: captchaAnswerHash(challengeId, answer),
            ttlMs: CAPTCHA_TTL_MS,
        });
        return {
            challengeId,
            image: renderCaptchaSvg(answer),
            expiresAt,
        };
    }

    verifyCaptcha({ challengeId, answer, action }) {
        if (typeof challengeId !== 'string' ||
            !CAPTCHA_ID_PATTERN.test(challengeId) ||
            typeof answer !== 'string' || !/^[2-9]{6}$/.test(answer.trim())) {
            throw new EmailAuthError('CAPTCHA_INVALID', 'The verification code is invalid.');
        }
        const expected = this.store.consumeCaptcha({ challengeId, action });
        const actual = captchaAnswerHash(challengeId, answer.trim());
        if (!expected || !timingSafeEqualHex(expected, actual)) {
            throw new EmailAuthError('CAPTCHA_INVALID', 'The verification code is invalid.');
        }
    }

    async requestRegistration(input, remoteKey) {
        this.requireEnabled();
        const email = normalizeEmail(input.email);
        const locale = normalizeLocale(input.locale);
        this.registrationLimiter.check(`ip:${remoteKey}`);
        this.registrationLimiter.check(`email:${hashToken(email).slice(0, 24)}`);
        this.verifyCaptcha({
            challengeId: input.captchaChallengeId,
            answer: input.captchaAnswer,
            action: 'register',
        });

        if (this.store.hasEmail(email)) {
            return { accepted: true };
        }
        const token = randomToken();
        const tokenHash = hashToken(token);
        this.store.createEmailVerification({
            tokenHash,
            email,
            locale,
            ttlMs: VERIFICATION_TTL_MS,
        });
        try {
            await this.mailer.sendVerification({ email, token, locale });
        } catch (error) {
            this.store.deleteEmailVerification(tokenHash);
            throw error;
        }
        return {
            accepted: true,
            ...(this.exposeVerificationToken
                ? { verificationToken: token }
                : {}),
        };
    }

    async completeRegistration(input, remoteKey = 'unknown') {
        this.requireEnabled();
        this.completionLimiter.check(`ip:${remoteKey}`);
        if (typeof input.token !== 'string' || !TOKEN_PATTERN.test(input.token)) {
            throw new EmailAuthError(
                'VERIFICATION_LINK_INVALID',
                'The verification link is invalid or expired.'
            );
        }
        const password = validatePassword(input.password);
        const verificationHash = hashToken(input.token);
        const emailRecord = this.store.findPendingEmailVerification(
            verificationHash
        );
        if (!emailRecord) {
            throw new EmailAuthError(
                'VERIFICATION_LINK_INVALID',
                'The verification link is invalid or expired.'
            );
        }
        const passwordHash = await argon2Hash(password, {
            algorithm: Algorithm.Argon2id,
            memoryCost: 19456,
            timeCost: 2,
            parallelism: 1,
            outputLen: 32,
        });
        const result = this.store.completeRegistration({
            tokenHash: verificationHash,
            passwordHash,
            displayName: emailRecord
                ? deriveDisplayName(emailRecord.email)
                : 'User',
        });
        if (!result || result.alreadyRegistered) {
            throw new EmailAuthError(
                'VERIFICATION_LINK_INVALID',
                'The verification link is invalid or expired.'
            );
        }
        return this.issueSession(result);
    }

    async dummyHash() {
        if (!this.dummyHashPromise) {
            this.dummyHashPromise = argon2Hash('not-a-real-password', {
                algorithm: Algorithm.Argon2id,
                memoryCost: 19456,
                timeCost: 2,
                parallelism: 1,
                outputLen: 32,
            });
        }
        return this.dummyHashPromise;
    }

    async login(input, remoteKey) {
        this.requireEnabled();
        const email = normalizeEmail(input.email);
        const password = validatePassword(input.password);
        this.loginLimiter.check(`ip:${remoteKey}`);
        this.loginLimiter.check(`email:${hashToken(email).slice(0, 24)}`);
        this.verifyCaptcha({
            challengeId: input.captchaChallengeId,
            answer: input.captchaAnswer,
            action: 'login',
        });
        const credential = this.store.findCredentialByEmail(email);
        const hashToCheck = credential
            ? credential.passwordHash
            : await this.dummyHash();
        let valid = false;
        try {
            valid = await argon2Verify(hashToCheck, password);
        } catch (error) {
            valid = false;
        }
        if (!credential || !valid) {
            throw new EmailAuthError(
                'INVALID_CREDENTIALS',
                'Email or password is incorrect.',
                401
            );
        }
        this.store.recordLogin(credential.uid);
        return this.issueSession(credential);
    }

    issueSession(user) {
        const token = randomToken();
        const { expiresAt } = this.store.createSession({
            tokenHash: hashToken(token),
            userUid: user.uid,
            ttlMs: SESSION_TTL_MS,
        });
        return {
            token,
            expiresAt,
            user: {
                uid: user.uid,
                email: user.email,
                displayName: user.displayName,
                role: user.role,
                avatarId: user.avatarId || null,
                avatarUrl: avatarUrl(user.avatarId),
            },
        };
    }

    updateAvatar(rawToken, value) {
        const session = this.resolveSession(rawToken);
        if (!session) {
            throw new EmailAuthError('AUTH_REQUIRED', 'Sign in is required.', 401);
        }
        const avatarId = normalizeAvatarId(value);
        if (!avatarId) {
            throw new EmailAuthError('INVALID_AVATAR', 'Invalid avatar selection.', 400);
        }
        const user = this.store.changeUserAvatar({
            userUid: session.uid,
            avatarId,
        });
        if (!user) {
            throw new EmailAuthError('AUTH_REQUIRED', 'Sign in is required.', 401);
        }
        return {
            uid: user.uid,
            email: user.email,
            displayName: user.displayName,
            role: user.role,
            avatarId: user.avatarId || null,
            avatarUrl: avatarUrl(user.avatarId),
        };
    }

    resolveSession(rawToken) {
        if (typeof rawToken !== 'string' || !TOKEN_PATTERN.test(rawToken)) {
            return null;
        }
        return this.store.findSession(hashToken(rawToken));
    }

    revokeSession(rawToken) {
        if (typeof rawToken === 'string' && TOKEN_PATTERN.test(rawToken)) {
            this.store.revokeSession(hashToken(rawToken));
        }
    }

    readSessionToken(req) {
        return parseCookies(req.headers.cookie)[this.cookieName] || null;
    }

    setSessionCookie(res, token) {
        res.setHeader('Set-Cookie', sessionCookie(
            this.cookieName,
            token,
            this.secureCookie,
            SESSION_TTL_MS / 1000
        ));
    }

    clearSessionCookie(res) {
        res.setHeader('Set-Cookie', sessionCookie(
            this.cookieName,
            '',
            this.secureCookie,
            0
        ));
    }
}

function readSecret(environment, inlineName, fileName) {
    const inline = String(environment[inlineName] || '').trim();
    const configuredFile = String(environment[fileName] || '').trim();
    if (inline && configuredFile) {
        throw new Error(`${inlineName} and ${fileName} cannot both be set`);
    }
    if (configuredFile) {
        if (environment.NODE_ENV === 'production' &&
            !path.isAbsolute(configuredFile)) {
            throw new Error(`${fileName} must be absolute in production`);
        }
        return fs.readFileSync(configuredFile, 'utf8').trim();
    }
    if (environment.NODE_ENV === 'production' && inline) {
        throw new Error(`${fileName} is required instead of ${inlineName} in production`);
    }
    return inline;
}

function exactPublicOrigin(value) {
    let parsed;
    try {
        parsed = new URL(String(value || ''));
    } catch (error) {
        throw new Error('USER_AUTH_PUBLIC_ORIGIN must be an exact URL origin');
    }
    if (parsed.origin !== String(value) ||
        (parsed.protocol !== 'https:' &&
         parsed.hostname !== 'localhost' &&
         parsed.hostname !== '127.0.0.1')) {
        throw new Error('USER_AUTH_PUBLIC_ORIGIN must be an exact HTTPS origin');
    }
    return parsed.origin;
}

function isLoopbackOrigin(origin) {
    const hostname = new URL(origin).hostname;
    return hostname === 'localhost' || hostname === '127.0.0.1' ||
        hostname === '[::1]';
}

function createEmailAuthFromEnvironment(options) {
    const environment = options.environment || process.env;
    const enabled = environment.USER_AUTH_ENABLED === '1';
    if (!enabled) {
        return new EmailAuthService({
            store: options.store,
            enabled: false,
            allowedOrigins: options.allowedOrigins,
        });
    }
    if (!String(environment.USER_AUTH_PUBLIC_ORIGIN || '').trim()) {
        throw new Error('USER_AUTH_PUBLIC_ORIGIN is required when user auth is enabled');
    }
    const publicOrigin = exactPublicOrigin(environment.USER_AUTH_PUBLIC_ORIGIN);
    if (!options.allowedOrigins.includes(publicOrigin)) {
        throw new Error('USER_AUTH_PUBLIC_ORIGIN must be present in WEB_CONFIG_ORIGINS');
    }
    const localPreview = environment.USER_AUTH_LOCAL_PREVIEW === '1';
    if (localPreview) {
        if (environment.NODE_ENV === 'production' ||
            !isLoopbackOrigin(publicOrigin)) {
            throw new Error(
                'USER_AUTH_LOCAL_PREVIEW is restricted to loopback development'
            );
        }
        return new EmailAuthService({
            store: options.store,
            enabled: true,
            allowedOrigins: options.allowedOrigins,
            publicOrigin,
            mailer: new LocalPreviewMailer(),
            exposeVerificationToken: true,
        });
    }
    const apiKey = readSecret(
        environment,
        'RESEND_API_KEY',
        'RESEND_API_KEY_FILE'
    );
    if (!apiKey) {
        throw new Error('RESEND_API_KEY_FILE is required when user auth is enabled');
    }
    const from = String(
        environment.USER_AUTH_EMAIL_FROM ||
        'ST-Dash <no-reply@auth.st-dash.com>'
    ).trim();
    if (!from.includes('@')) {
        throw new Error('USER_AUTH_EMAIL_FROM must contain an email address');
    }
    return new EmailAuthService({
        store: options.store,
        enabled: true,
        allowedOrigins: options.allowedOrigins,
        publicOrigin,
        mailer: new ResendMailer({ apiKey, from, publicOrigin }),
    });
}

function sendAuthError(res, error) {
    const status = error instanceof EmailAuthError ? error.status : 500;
    if (status >= 500 && !(error instanceof EmailAuthError)) {
        console.error('Email authentication error:', error);
    }
    res.status(status).json({
        error: error instanceof EmailAuthError
            ? error.code
            : 'AUTH_SERVER_ERROR',
        message: status >= 500 && !(error instanceof EmailAuthError)
            ? 'Authentication is temporarily unavailable.'
            : error.message,
    });
}

function initEmailAuthRoutes(app, service) {
    const asyncRoute = handler => async (req, res) => {
        try {
            res.set('Cache-Control', 'no-store');
            await handler(req, res);
        } catch (error) {
            sendAuthError(res, error);
        }
    };
    const requireOrigin = req => service.requireOrigin(req.get('Origin'));
    const remoteKey = req => req.ip || req.socket?.remoteAddress || 'unknown';

    app.get('/api/auth/session', asyncRoute(async (req, res) => {
        const session = service.resolveSession(service.readSessionToken(req));
        if (!session) {
            return res.json({
                authenticated: false,
                registrationEnabled: service.enabled,
            });
        }
        return res.json({
            authenticated: true,
            user: {
                uid: session.uid,
                email: session.email,
                displayName: session.displayName,
                role: session.role,
                avatarId: session.avatarId || null,
                avatarUrl: avatarUrl(session.avatarId),
            },
            expiresAt: session.expiresAt,
            registrationEnabled: service.enabled,
        });
    }));

    app.post('/api/auth/captcha', asyncRoute(async (req, res) => {
        requireOrigin(req);
        res.status(201).json(service.createCaptcha(req.body?.action, remoteKey(req)));
    }));

    app.post('/api/auth/register/email/request', asyncRoute(async (req, res) => {
        requireOrigin(req);
        const result = await service.requestRegistration(
            req.body || {},
            remoteKey(req)
        );
        res.status(202).json(result);
    }));

    app.post('/api/auth/register/email/complete', asyncRoute(async (req, res) => {
        requireOrigin(req);
        const session = await service.completeRegistration(
            req.body || {},
            remoteKey(req)
        );
        service.setSessionCookie(res, session.token);
        res.status(201).json({
            authenticated: true,
            user: session.user,
            expiresAt: session.expiresAt,
            registrationEnabled: true,
        });
    }));

    app.post('/api/auth/login/email', asyncRoute(async (req, res) => {
        requireOrigin(req);
        const session = await service.login(req.body || {}, remoteKey(req));
        service.setSessionCookie(res, session.token);
        res.json({
            authenticated: true,
            user: session.user,
            expiresAt: session.expiresAt,
            registrationEnabled: true,
        });
    }));

    app.post('/api/auth/logout', asyncRoute(async (req, res) => {
        requireOrigin(req);
        service.revokeSession(service.readSessionToken(req));
        service.clearSessionCookie(res);
        res.status(204).end();
    }));

    app.patch('/api/auth/profile/avatar', asyncRoute(async (req, res) => {
        requireOrigin(req);
        const user = service.updateAvatar(
            service.readSessionToken(req),
            req.body?.avatarId
        );
        res.json({ user });
    }));
}

module.exports = {
    CAPTCHA_TTL_MS,
    PASSWORD_MAX_LENGTH,
    PASSWORD_MIN_LENGTH,
    SESSION_TTL_MS,
    VERIFICATION_TTL_MS,
    EmailAuthError,
    EmailAuthService,
    LocalPreviewMailer,
    ResendMailer,
    SlidingWindowRateLimiter,
    captchaAnswerHash,
    createCaptchaAnswer,
    createEmailAuthFromEnvironment,
    exactPublicOrigin,
    hashToken,
    initEmailAuthRoutes,
    normalizeEmail,
    renderCaptchaSvg,
    sessionCookie,
    validatePassword,
};
