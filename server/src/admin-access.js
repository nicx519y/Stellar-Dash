'use strict';

const crypto = require('crypto');

const SERVICE_TOKEN_PREFIX = 'stsvc_';
const SERVICE_TOKEN_PATTERN = /^stsvc_[A-Za-z0-9_-]{43}$/;
const SERVICE_TOKEN_SCOPES = Object.freeze([
    'device.manage',
    'firmware.manage',
]);
const DEFAULT_SERVICE_TOKEN_DAYS = 90;
const MAX_SERVICE_TOKEN_DAYS = 365;

class AdminAccessError extends Error {
    constructor(code, message, status) {
        super(message);
        this.name = 'AdminAccessError';
        this.code = code;
        this.status = status;
    }
}

function hashServiceToken(token) {
    return crypto.createHash('sha256').update(token).digest('hex');
}

function createRawServiceToken() {
    return `${SERVICE_TOKEN_PREFIX}${crypto.randomBytes(32).toString('base64url')}`;
}

function normalizeServiceScopes(value) {
    if (!Array.isArray(value) || value.length === 0) {
        throw new AdminAccessError(
            'INVALID_SERVICE_TOKEN_SCOPES',
            'Select at least one service-token scope.',
            400
        );
    }
    const scopes = [...new Set(value)];
    if (scopes.some(scope => !SERVICE_TOKEN_SCOPES.includes(scope))) {
        throw new AdminAccessError(
            'INVALID_SERVICE_TOKEN_SCOPES',
            'The service-token scopes are invalid.',
            400
        );
    }
    return scopes;
}

function sendAdminError(res, error) {
    const known = error instanceof AdminAccessError ||
        Number.isInteger(error?.status);
    const status = known ? error.status : 500;
    if (!known) console.error('Administrator access error:', error);
    return res.status(status).json({
        success: false,
        error: known ? (error.code || 'ADMIN_ACCESS_ERROR') : 'ADMIN_ACCESS_ERROR',
        message: known ? error.message : 'Administrator access failed.',
    });
}

class AdminAccessService {
    constructor(options) {
        this.store = options.store;
        this.emailAuth = options.emailAuth;
        this.localTokenHash = options.localToken
            ? hashServiceToken(options.localToken)
            : null;
        this.now = options.now || Date.now;
    }

    resolveServiceToken(rawToken, requiredScope) {
        if (!SERVICE_TOKEN_PATTERN.test(rawToken)) return null;
        const tokenHash = hashServiceToken(rawToken);
        if (this.localTokenHash && tokenHash === this.localTokenHash) {
            if (requiredScope !== 'device.manage') {
                throw new AdminAccessError(
                    'SERVICE_TOKEN_SCOPE_REQUIRED',
                    'The service token does not grant this scope.',
                    403
                );
            }
            return {
                actorType: 'service',
                actorId: 'local-loopback',
                username: 'local-loopback',
                role: 'service',
                scopes: ['device.manage'],
            };
        }
        const record = this.store.findServiceTokenByHash(tokenHash);
        if (!record || record.revokedAt !== null ||
            record.expiresAt <= this.now()) {
            return null;
        }
        if (!record.scopes.includes(requiredScope)) {
            throw new AdminAccessError(
                'SERVICE_TOKEN_SCOPE_REQUIRED',
                'The service token does not grant this scope.',
                403
            );
        }
        this.store.recordServiceTokenUse(record.id);
        return {
            actorType: 'service',
            actorId: record.id,
            username: record.name,
            role: 'service',
            scopes: record.scopes,
        };
    }

    resolveUser(req) {
        const session = this.emailAuth.resolveSession(
            this.emailAuth.readSessionToken(req)
        );
        if (!session) return null;
        if (session.role !== 'admin') {
            throw new AdminAccessError(
                'ADMIN_ROLE_REQUIRED',
                'Administrator role is required.',
                403
            );
        }
        return {
            actorType: 'user',
            actorId: session.uid,
            uid: session.uid,
            email: session.email,
            displayName: session.displayName,
            username: session.email,
            role: session.role,
        };
    }

    requireAdmin(options = {}) {
        const requiredScope = options.serviceScope || null;
        const humanOnly = Boolean(options.humanOnly);
        return (req, res, next) => {
            try {
                const authorization = String(req.get('Authorization') || '');
                if (authorization.startsWith('Basic ')) {
                    throw new AdminAccessError(
                        'BASIC_AUTH_RETIRED',
                        'Basic administrator authentication has been retired.',
                        401
                    );
                }
                let actor = null;
                if (authorization.startsWith('Bearer ')) {
                    if (humanOnly || !requiredScope) {
                        throw new AdminAccessError(
                            'HUMAN_ADMIN_REQUIRED',
                            'An email administrator session is required.',
                            403
                        );
                    }
                    actor = this.resolveServiceToken(
                        authorization.slice(7).trim(),
                        requiredScope
                    );
                } else {
                    actor = this.resolveUser(req);
                    if (actor && !['GET', 'HEAD', 'OPTIONS'].includes(req.method)) {
                        this.emailAuth.requireOrigin(req.get('Origin'));
                    }
                }
                if (!actor) {
                    throw new AdminAccessError(
                        'ADMIN_AUTH_REQUIRED',
                        'Administrator authentication is required.',
                        401
                    );
                }
                req.authenticatedAdmin = actor;
                next();
            } catch (error) {
                sendAdminError(res, error);
            }
        };
    }
}

function createAdminAccessFromEnvironment(options) {
    const environment = options.environment || process.env;
    const localToken = String(
        environment.HBOX_LOCAL_ADMIN_SERVICE_TOKEN || ''
    ).trim();
    if (localToken) {
        const listenHost = String(environment.LISTEN_HOST || '').trim();
        if (environment.NODE_ENV === 'production' ||
            !['127.0.0.1', 'localhost', '::1'].includes(listenHost) ||
            !SERVICE_TOKEN_PATTERN.test(localToken)) {
            throw new Error(
                'HBOX_LOCAL_ADMIN_SERVICE_TOKEN is restricted to loopback development'
            );
        }
    }
    return new AdminAccessService({
        store: options.store,
        emailAuth: options.emailAuth,
        localToken: localToken || null,
    });
}

function initAdminRoutes(app, service) {
    const humanAdmin = service.requireAdmin({ humanOnly: true });
    const asyncRoute = handler => async (req, res) => {
        try {
            res.set('Cache-Control', 'no-store');
            await handler(req, res);
        } catch (error) {
            sendAdminError(res, error);
        }
    };

    app.get('/api/admin/profile', humanAdmin, asyncRoute(async (req, res) => {
        res.json({
            success: true,
            data: {
                uid: req.authenticatedAdmin.uid,
                email: req.authenticatedAdmin.email,
                displayName: req.authenticatedAdmin.displayName,
                role: 'admin',
            },
        });
    }));

    app.get('/api/admin/users', humanAdmin, asyncRoute(async (req, res) => {
        const limit = Number.parseInt(req.query.limit, 10) || 50;
        const offset = Number.parseInt(req.query.offset, 10) || 0;
        const result = service.store.listUsers({
            query: req.query.query || '',
            limit,
            offset,
        });
        res.json({ success: true, data: result });
    }));

    app.patch(
        '/api/admin/users/:uid/role',
        humanAdmin,
        asyncRoute(async (req, res) => {
            let user;
            try {
                user = service.store.changeUserRole({
                    userUid: req.params.uid,
                    role: req.body?.role,
                    actorUid: req.authenticatedAdmin.uid,
                });
            } catch (error) {
                if (error.code === 'LAST_ADMIN_REQUIRED') {
                    throw new AdminAccessError(
                        error.code,
                        error.message,
                        409
                    );
                }
                if (error instanceof TypeError) {
                    throw new AdminAccessError(
                        'INVALID_ACCOUNT_ROLE',
                        error.message,
                        400
                    );
                }
                throw error;
            }
            if (!user) {
                throw new AdminAccessError(
                    'USER_NOT_FOUND',
                    'The user was not found.',
                    404
                );
            }
            res.json({ success: true, data: { user } });
        })
    );

    app.get(
        '/api/admin/service-tokens',
        humanAdmin,
        asyncRoute(async (req, res) => {
            const tokens = service.store.listServiceTokens().map(token => ({
                id: token.id,
                name: token.name,
                scopes: token.scopes,
                expiresAt: token.expiresAt,
                revokedAt: token.revokedAt,
            }));
            res.json({
                success: true,
                data: { tokens },
            });
        })
    );

    app.post(
        '/api/admin/service-tokens',
        humanAdmin,
        asyncRoute(async (req, res) => {
            const name = String(req.body?.name || '').trim();
            if (!name || name.length > 80) {
                throw new AdminAccessError(
                    'INVALID_SERVICE_TOKEN_NAME',
                    'Service-token name must be 1-80 characters.',
                    400
                );
            }
            const scopes = normalizeServiceScopes(req.body?.scopes);
            const expiresInDays = req.body?.expiresInDays === undefined
                ? DEFAULT_SERVICE_TOKEN_DAYS
                : Number(req.body.expiresInDays);
            if (!Number.isInteger(expiresInDays) || expiresInDays < 1 ||
                expiresInDays > MAX_SERVICE_TOKEN_DAYS) {
                throw new AdminAccessError(
                    'INVALID_SERVICE_TOKEN_EXPIRY',
                    `Service-token expiry must be 1-${MAX_SERVICE_TOKEN_DAYS} days.`,
                    400
                );
            }
            const secret = createRawServiceToken();
            const token = service.store.createServiceToken({
                id: crypto.randomUUID(),
                name,
                tokenHash: hashServiceToken(secret),
                scopes,
                createdByUserUid: req.authenticatedAdmin.uid,
                expiresAt: Date.now() + expiresInDays * 24 * 60 * 60 * 1000,
            });
            res.status(201).json({
                success: true,
                data: {
                    token: {
                        id: token.id,
                        name: token.name,
                        scopes: token.scopes,
                        createdAt: token.createdAt,
                        expiresAt: token.expiresAt,
                    },
                    secret,
                },
            });
        })
    );

    app.delete(
        '/api/admin/service-tokens/:id',
        humanAdmin,
        asyncRoute(async (req, res) => {
            const revoked = service.store.revokeServiceToken({
                id: req.params.id,
                actorUid: req.authenticatedAdmin.uid,
            });
            if (!revoked) {
                throw new AdminAccessError(
                    'SERVICE_TOKEN_NOT_FOUND',
                    'The active service token was not found.',
                    404
                );
            }
            res.status(204).end();
        })
    );
}

module.exports = {
    DEFAULT_SERVICE_TOKEN_DAYS,
    MAX_SERVICE_TOKEN_DAYS,
    SERVICE_TOKEN_PATTERN,
    SERVICE_TOKEN_SCOPES,
    AdminAccessError,
    AdminAccessService,
    createAdminAccessFromEnvironment,
    createRawServiceToken,
    hashServiceToken,
    initAdminRoutes,
    normalizeServiceScopes,
};
