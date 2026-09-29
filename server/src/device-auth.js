#!/usr/bin/env node
'use strict';

/*
 * Legacy V1 authentication is retained only for already shipped hardware.
 * It is not a proof of genuine hardware and must never authorize V2
 * WebConfig sessions. Sensitive challenge and signature material is
 * deliberately excluded from logs.
 */

const DEFAULT_CONFIG = Object.freeze({
    expiresIn: 2 * 60
});

const usedChallenges = new Map();

function generateDeviceSignature(deviceId, challenge, timestamp) {
    const signData = deviceId + challenge + timestamp.toString();
    let hash = 0x9E3779B9;
    for (let index = 0; index < signData.length; index += 1) {
        hash = (((hash << 5) + hash) + signData.charCodeAt(index)) >>> 0;
    }
    return `SIG_${hash.toString(16).toUpperCase().padStart(8, '0')}`;
}

function readAuthData(req, source) {
    if (source === 'body') {
        return req.body && req.body.deviceAuth;
    }
    const encoded = source === 'query'
        ? req.query && req.query.deviceAuth
        : req.headers['x-device-auth'];
    if (!encoded) {
        return null;
    }
    try {
        return JSON.parse(Buffer.from(encoded, 'base64').toString('utf8'));
    } catch (error) {
        const invalid = new Error('Invalid authentication format');
        invalid.code = 'AUTH_INVALID_FORMAT';
        throw invalid;
    }
}

function validateDeviceAuth(options = {}) {
    const config = { ...DEFAULT_CONFIG, ...options };
    const source = config.source || 'headers';
    return (req, res, next) => {
        try {
            const authData = readAuthData(req, source);
            if (!authData) {
                return res.status(401).json({
                    error: 'AUTH_MISSING',
                    message: 'Device authentication required'
                });
            }
            const { deviceId, challenge, timestamp, signature } = authData;
            if (typeof deviceId !== 'string' || deviceId.length === 0 ||
                typeof challenge !== 'string' || challenge.length === 0 ||
                !Number.isFinite(Number(timestamp)) ||
                typeof signature !== 'string' || signature.length === 0) {
                return res.status(401).json({
                    error: 'AUTH_INCOMPLETE',
                    message: 'Authentication data incomplete'
                });
            }
            const storage = req.app.locals.storage_manager;
            if (!storage) {
                return res.status(500).json({
                    error: 'SERVER_ERROR',
                    message: 'Storage manager not initialized'
                });
            }
            const device = storage.findDevice(deviceId);
            if (!device) {
                return res.status(401).json({
                    error: 'DEVICE_NOT_REGISTERED',
                    message: 'Device not registered'
                });
            }
            if (signature !== generateDeviceSignature(
                deviceId,
                challenge,
                timestamp
            )) {
                return res.status(401).json({
                    error: 'INVALID_SIGNATURE',
                    message: 'Invalid device signature'
                });
            }

            const now = Date.now();
            const expiresMs = config.expiresIn * 1000;
            const firstUsed = usedChallenges.get(challenge);
            if (firstUsed && now - firstUsed > expiresMs) {
                return res.status(401).json({
                    error: 'CHALLENGE_EXPIRED',
                    message: 'Challenge has expired'
                });
            }
            if (!firstUsed) {
                usedChallenges.set(challenge, now);
            }
            for (const [value, usedAt] of usedChallenges.entries()) {
                if (usedAt < now - expiresMs) {
                    usedChallenges.delete(value);
                }
            }

            req.authenticatedDevice = device;
            return next();
        } catch (error) {
            if (error.code === 'AUTH_INVALID_FORMAT') {
                return res.status(401).json({
                    error: error.code,
                    message: error.message
                });
            }
            console.error('Legacy device authentication failed');
            return res.status(500).json({
                error: 'AUTH_SERVER_ERROR',
                message: 'Authentication server error'
            });
        }
    };
}

module.exports = {
    generateDeviceSignature,
    validateDeviceAuth
};
