'use strict';

function parseAllowedOrigins(value, defaultOrigin) {
    const candidates = value
        ? value.split(',')
        : [defaultOrigin];
    const origins = candidates
        .map(origin => origin.trim())
        .filter(Boolean)
        .map(origin => {
            let parsed;
            try {
                parsed = new URL(origin);
            } catch (error) {
                throw new Error(`invalid WebConfig origin: ${origin}`);
            }
            if (parsed.origin !== origin ||
                (parsed.protocol !== 'https:' &&
                 parsed.hostname !== 'localhost' &&
                 parsed.hostname !== '127.0.0.1')) {
                throw new Error(
                    `WebConfig origin must be an exact HTTPS origin: ${origin}`
                );
            }
            return parsed.origin;
        });
    if (origins.length === 0) {
        throw new Error('at least one WebConfig origin is required');
    }
    return [...new Set(origins)];
}

function parseTrustedProxyHops(value) {
    if (value === undefined || value === null || value === '') {
        return 0;
    }
    if (!/^[0-2]$/.test(String(value))) {
        throw new Error('TRUST_PROXY_HOPS must be 0, 1, or 2');
    }
    return Number.parseInt(value, 10);
}

function createExactCorsOptions(allowedOrigins) {
    const allowlist = new Set(allowedOrigins);
    return {
        origin(origin, callback) {
            /*
             * Requests without Origin are not CORS requests. Endpoints which
             * require a browser origin (notably challenge issuance) perform
             * their own fail-closed check.
             */
            if (!origin) {
                return callback(null, false);
            }
            if (!allowlist.has(origin)) {
                const error = new Error('origin is not allowed');
                error.status = 403;
                error.code = 'ORIGIN_NOT_ALLOWED';
                return callback(error);
            }
            return callback(null, origin);
        },
        methods: ['GET', 'HEAD', 'POST', 'PUT', 'DELETE', 'OPTIONS'],
        allowedHeaders: [
            'Authorization',
            'Content-Type',
            'X-Device-Auth'
        ],
        exposedHeaders: ['Content-Length', 'Content-Range'],
        credentials: false,
        maxAge: 600,
        optionsSuccessStatus: 204
    };
}

function buildContentSecurityPolicy(options = {}) {
    const scriptHashes = Array.isArray(options.scriptHashes)
        ? options.scriptHashes
        : [];
    const normalizedHashes = [...new Set(scriptHashes.map(value => {
        if (typeof value !== 'string' ||
            !/^sha256-[A-Za-z0-9+/]{43}=$/.test(value)) {
            throw new TypeError('scriptHashes must contain CSP SHA-256 values');
        }
        return `'${value}'`;
    }))];

    return [
        "default-src 'self'",
        "base-uri 'self'",
        "frame-ancestors 'none'",
        "object-src 'none'",
        ["script-src 'self'", ...normalizedHashes].join(' '),
        "script-src-attr 'none'",
        "style-src 'self' 'unsafe-inline'",
        // Device previews are fetched as authenticated same-origin responses
        // and rendered through an in-memory Blob URL. Keep remote image
        // origins blocked while allowing that local object URL lifecycle.
        "img-src 'self' data: blob:",
        "connect-src 'self'",
        "font-src 'self'",
        "form-action 'self'"
    ].join('; ');
}

function securityHeaders(req, res, next) {
    res.set({
        'Strict-Transport-Security':
            'max-age=31536000; includeSubDomains',
        'Content-Security-Policy': buildContentSecurityPolicy(),
        'Permissions-Policy':
            'hid=(self), camera=(), microphone=(), geolocation=()',
        'Referrer-Policy': 'no-referrer',
        'X-Content-Type-Options': 'nosniff',
        'X-Frame-Options': 'DENY',
        'Cross-Origin-Opener-Policy': 'same-origin',
        'Cross-Origin-Resource-Policy': 'same-origin'
    });
    next();
}

module.exports = {
    parseAllowedOrigins,
    parseTrustedProxyHops,
    createExactCorsOptions,
    buildContentSecurityPolicy,
    securityHeaders
};
