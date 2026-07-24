#!/usr/bin/env node
'use strict';

const crypto = require('crypto');

const CHALLENGE_TTL_MS = 60 * 1000;
const SESSION_TTL_MS = 5 * 60 * 1000;
const MAX_ENVELOPE_BYTES = 16 * 1024;
const MAGIC = Object.freeze({
    deviceCertificate: 0x31434448,
    bootAttestation: 0x31414248,
    attestationTranscript: 0x31544148,
    sessionPermit: 0x31505348
});
const STRUCT_SIZE = Object.freeze({
    deviceCertificate: 208,
    bootAttestation: 229,
    attestationTranscript: 354,
    sessionPermit: 236
});
const SIGNED_SIZE = Object.freeze({
    deviceCertificate: 144,
    bootAttestation: 165,
    attestationTranscript: 290,
    sessionPermit: 172
});

const DEVICE_AUTH_SCOPES = Object.freeze({
    'config.read': 1 << 0,
    'config.write': 1 << 1,
    'monitor.read': 1 << 2,
    'device.control': 1 << 3,
    'asset.write': 1 << 4,
    'firmware.update': 1 << 5
});

class DeviceAuthV2Error extends Error {
    constructor(code, message, status = 401) {
        super(message);
        this.name = 'DeviceAuthV2Error';
        this.code = code;
        this.status = status;
    }
}

class SlidingWindowRateLimiter {
    constructor(options = {}) {
        this.now = options.now || Date.now;
        this.windowMs = options.windowMs || 60 * 1000;
        this.maxAttempts = options.maxAttempts || 20;
        this.maxKeys = options.maxKeys || 10000;
        this.attempts = new Map();
        this.operations = 0;
    }

    check(key) {
        const normalizedKey = String(key || 'unknown');
        const now = this.now();
        const cutoff = now - this.windowMs;
        let timestamps = this.attempts.get(normalizedKey) || [];
        timestamps = timestamps.filter(timestamp => timestamp > cutoff);

        if (timestamps.length >= this.maxAttempts) {
            const error = new DeviceAuthV2Error(
                'AUTH_RATE_LIMITED',
                'too many device authentication attempts',
                429
            );
            error.retryAfterSeconds = Math.max(
                1,
                Math.ceil(
                    (timestamps[0] + this.windowMs - now) / 1000
                )
            );
            throw error;
        }
        if (!this.attempts.has(normalizedKey) &&
            this.attempts.size >= this.maxKeys) {
            throw new DeviceAuthV2Error(
                'AUTH_RATE_LIMIT_CAPACITY',
                'authentication rate limiter capacity is exhausted',
                503
            );
        }
        timestamps.push(now);
        this.attempts.set(normalizedKey, timestamps);

        /*
         * Keep memory bounded without walking the map on every request.
         * Production multi-process deployments inject a shared limiter with
         * an equivalent atomic operation.
         */
        this.operations += 1;
        if ((this.operations & 0xff) === 0) {
            for (const [entryKey, values] of this.attempts.entries()) {
                const active = values.filter(timestamp => timestamp > cutoff);
                if (active.length === 0) {
                    this.attempts.delete(entryKey);
                } else {
                    this.attempts.set(entryKey, active);
                }
            }
        }
    }
}

function encodeBase64Url(value) {
    return Buffer.from(value).toString('base64')
        .replace(/\+/g, '-')
        .replace(/\//g, '_')
        .replace(/=+$/g, '');
}

function encodeBase64(value) {
    return Buffer.from(value).toString('base64');
}

function decodeBase64Url(value, label, expectedLength = null) {
    if (typeof value !== 'string' ||
        value.length === 0 ||
        !/^[A-Za-z0-9_-]+$/.test(value)) {
        throw new DeviceAuthV2Error(
            'INVALID_BINARY_FIELD',
            `${label} must be unpadded base64url`,
            400
        );
    }
    const padding = '='.repeat((4 - (value.length % 4)) % 4);
    const decoded = Buffer.from(
        value.replace(/-/g, '+').replace(/_/g, '/') + padding,
        'base64'
    );
    if (encodeBase64Url(decoded) !== value ||
        (expectedLength !== null && decoded.length !== expectedLength)) {
        throw new DeviceAuthV2Error(
            'INVALID_BINARY_FIELD',
            `${label} has an invalid encoding or length`,
            400
        );
    }
    return decoded;
}

function decodeWireBase64(value, label, expectedLength = null) {
    if (typeof value !== 'string' || value.length === 0) {
        throw new DeviceAuthV2Error(
            'INVALID_BINARY_FIELD',
            `${label} must be base64`,
            400
        );
    }
    if (/^[A-Za-z0-9_-]+$/.test(value)) {
        return decodeBase64Url(value, label, expectedLength);
    }
    if (!/^[A-Za-z0-9+/]+={0,2}$/.test(value) ||
        value.length % 4 !== 0) {
        throw new DeviceAuthV2Error(
            'INVALID_BINARY_FIELD',
            `${label} must be canonical base64 or base64url`,
            400
        );
    }
    const decoded = Buffer.from(value, 'base64');
    if (decoded.toString('base64') !== value ||
        (expectedLength !== null && decoded.length !== expectedLength)) {
        throw new DeviceAuthV2Error(
            'INVALID_BINARY_FIELD',
            `${label} has an invalid encoding or length`,
            400
        );
    }
    return decoded;
}

function normalizeP256PublicKey(encoded, label) {
    const raw = Buffer.isBuffer(encoded)
        ? encoded
        : decodeWireBase64(encoded, label);
    if (raw.length !== 65 || raw[0] !== 0x04) {
        throw new DeviceAuthV2Error(
            'INVALID_PUBLIC_KEY',
            `${label} must be a 65-byte uncompressed SEC1 P-256 key`,
            400
        );
    }
    let key;
    try {
        key = crypto.createPublicKey({
            key: {
                kty: 'EC',
                crv: 'P-256',
                x: encodeBase64Url(raw.subarray(1, 33)),
                y: encodeBase64Url(raw.subarray(33, 65))
            },
            format: 'jwk'
        });
    } catch (error) {
        throw new DeviceAuthV2Error(
            'INVALID_PUBLIC_KEY',
            `${label} is not a valid P-256 point`,
            400
        );
    }
    const details = key.asymmetricKeyDetails || {};
    if (key.asymmetricKeyType !== 'ec' ||
        details.namedCurve !== 'prime256v1') {
        throw new DeviceAuthV2Error(
            'INVALID_PUBLIC_KEY',
            `${label} must be a P-256 public key`,
            400
        );
    }
    return {
        key,
        raw: Buffer.from(raw)
    };
}

function parseSignedStructure(bytes, label, expectedMagic, expectedSize,
    expectedSignedSize, signedSizeOffset = 6) {
    if (!Buffer.isBuffer(bytes) || bytes.length !== expectedSize ||
        bytes.length > MAX_ENVELOPE_BYTES ||
        bytes.readUInt32LE(0) !== expectedMagic ||
        bytes[4] !== 1 ||
        bytes.readUInt16LE(signedSizeOffset) !== expectedSignedSize) {
        throw new DeviceAuthV2Error(
            `INVALID_${label.toUpperCase()}`,
            `${label} header or size is invalid`,
            401
        );
    }
    return {
        bytes,
        signedBytes: bytes.subarray(0, expectedSignedSize),
        signature: bytes.subarray(expectedSignedSize)
    };
}

function requireZero(bytes, label) {
    if (bytes.some(value => value !== 0)) {
        throw new DeviceAuthV2Error(
            'NONZERO_RESERVED_FIELD',
            `${label} must be zero`,
            401
        );
    }
}

function isAllZero(bytes) {
    return bytes.every(value => value === 0);
}

function hardwareVersionFromCode(code) {
    return `${(code >>> 16) & 0xff}.${(code >>> 8) & 0xff}.${code & 0xff}`;
}

function verifyP256(publicKey, data, signature, errorCode) {
    let valid = false;
    try {
        valid = crypto.verify(
            'sha256',
            Buffer.isBuffer(data) ? data : Buffer.from(data),
            { key: publicKey, dsaEncoding: 'ieee-p1363' },
            signature
        );
    } catch (error) {
        valid = false;
    }
    if (!valid) {
        throw new DeviceAuthV2Error(
            errorCode,
            'P-256 signature verification failed',
            401
        );
    }
}

function sha256(value) {
    return crypto.createHash('sha256').update(value).digest();
}

function normalizeScopes(scopes) {
    if (!Array.isArray(scopes) || scopes.length === 0 ||
        scopes.length > Object.keys(DEVICE_AUTH_SCOPES).length) {
        throw new DeviceAuthV2Error(
            'INVALID_SCOPES',
            'requestedScopes must be a non-empty scope array',
            400
        );
    }
    const unique = [...new Set(scopes)];
    if (unique.some(scope =>
        typeof scope !== 'string' ||
        !Object.prototype.hasOwnProperty.call(DEVICE_AUTH_SCOPES, scope))) {
        throw new DeviceAuthV2Error(
            'INVALID_SCOPES',
            'requestedScopes contains an unsupported scope',
            400
        );
    }
    return unique.sort();
}

function scopesToMask(scopes) {
    return normalizeScopes(scopes).reduce(
        (mask, scope) => mask | DEVICE_AUTH_SCOPES[scope],
        0
    ) >>> 0;
}

function normalizeFirmwareMeasurements(measurements) {
    if (!Array.isArray(measurements) || measurements.length > 128) {
        throw new DeviceAuthV2Error(
            'INVALID_FIRMWARE_POLICY',
            'allowedFirmwareMeasurements must be an array of at most 128 hashes',
            400
        );
    }
    const normalized = [...new Set(measurements.map(value => {
        if (typeof value !== 'string' || !/^[0-9a-fA-F]{64}$/.test(value)) {
            throw new DeviceAuthV2Error(
                'INVALID_FIRMWARE_POLICY',
                'firmware measurements must be 32-byte hexadecimal SHA-256 values',
                400
            );
        }
        return value.toLowerCase();
    }))];
    return normalized;
}

function normalizeSecurityVersion(value) {
    if (!Number.isSafeInteger(value) || value < 0 ||
        value > 0xffffffff) {
        throw new DeviceAuthV2Error(
            'INVALID_FIRMWARE_POLICY',
            'minSecurityVersion must be an unsigned 32-bit integer',
            400
        );
    }
    return value;
}

function safeEqual(left, right) {
    return left.length === right.length &&
        crypto.timingSafeEqual(left, right);
}

class MemoryChallengeStore {
    constructor(options = {}) {
        this.now = options.now || Date.now;
        this.ttlMs = options.ttlMs || CHALLENGE_TTL_MS;
        this.maxRecords = options.maxRecords || 10000;
        this.maxPerRemote = options.maxPerRemote || 32;
        this.records = new Map();
    }

    issue(binding) {
        this.cleanup();
        const remoteCount = [...this.records.values()].filter(record =>
            record.remoteAddress === binding.remoteAddress
        ).length;
        if (this.records.size >= this.maxRecords ||
            remoteCount >= this.maxPerRemote) {
            throw new DeviceAuthV2Error(
                'CHALLENGE_RATE_LIMITED',
                'too many outstanding authentication challenges',
                429
            );
        }
        let challengeId;
        do {
            challengeId = encodeBase64Url(crypto.randomBytes(16));
        } while (this.records.has(challengeId));
        const record = {
            ...binding,
            challengeId,
            nonce: crypto.randomBytes(32),
            createdAt: this.now(),
            expiresAt: this.now() + this.ttlMs
        };
        this.records.set(challengeId, record);
        return record;
    }

    consume(challengeId) {
        /*
         * Map#get + delete execute synchronously in one JavaScript turn. The
         * record is deleted before any crypto or storage await point, so two
         * concurrent HTTP handlers cannot successfully consume the same ID.
         * A clustered deployment must inject a Redis/DB store with equivalent
         * atomic GETDEL semantics.
         */
        const record = this.records.get(challengeId);
        if (record) {
            this.records.delete(challengeId);
        }
        if (!record) {
            throw new DeviceAuthV2Error(
                'CHALLENGE_UNKNOWN_OR_USED',
                'challenge is unknown or has already been consumed',
                401
            );
        }
        if (record.expiresAt <= this.now()) {
            throw new DeviceAuthV2Error(
                'CHALLENGE_EXPIRED',
                'challenge has expired',
                401
            );
        }
        return record;
    }

    cleanup() {
        const now = this.now();
        for (const [id, record] of this.records.entries()) {
            if (record.expiresAt <= now) {
                this.records.delete(id);
            }
        }
    }
}

class OpaqueTokenStore {
    constructor(options = {}) {
        this.now = options.now || Date.now;
        this.ttlMs = options.ttlMs || SESSION_TTL_MS;
        this.tokens = new Map();
    }

    tokenIndex(token) {
        /*
         * Never retain the bearer credential itself in the store key. This
         * also keeps heap snapshots and diagnostics from exposing a usable
         * token. Redis/database adapters must use the same one-way index (or
         * an HMAC with a deployment-only indexing key).
         */
        return sha256(Buffer.from(token, 'ascii')).toString('hex');
    }

    issue(session) {
        this.cleanup();
        let token;
        let tokenIndex;
        do {
            token = encodeBase64Url(crypto.randomBytes(32));
            tokenIndex = this.tokenIndex(token);
        } while (this.tokens.has(tokenIndex));
        const record = {
            ...session,
            issuedAt: this.now(),
            expiresAt: this.now() + this.ttlMs
        };
        this.tokens.set(tokenIndex, record);
        return { token, record };
    }

    resolve(token) {
        const tokenIndex = this.tokenIndex(token);
        const record = this.tokens.get(tokenIndex);
        if (!record || record.expiresAt <= this.now()) {
            if (record) {
                this.tokens.delete(tokenIndex);
            }
            return null;
        }
        return record;
    }

    revoke(token) {
        this.tokens.delete(this.tokenIndex(token));
    }

    cleanup() {
        const now = this.now();
        for (const [tokenIndex, record] of this.tokens.entries()) {
            if (record.expiresAt <= now) {
                this.tokens.delete(tokenIndex);
            }
        }
    }
}

class BinaryDeviceCertificateVerifier {
    constructor(caPublicKey) {
        if (!caPublicKey) {
            throw new Error('manufacturing CA public key is required');
        }
        this.caPublicKey = caPublicKey.type === 'public'
            ? caPublicKey
            : crypto.createPublicKey(caPublicKey);
        if (this.caPublicKey.asymmetricKeyType !== 'ec' ||
            (this.caPublicKey.asymmetricKeyDetails || {}).namedCurve !==
                'prime256v1') {
            throw new Error('manufacturing CA public key must be P-256');
        }
    }

    verify(certificateBytes, context = {}) {
        const parsed = parseSignedStructure(
            certificateBytes,
            'device_certificate',
            MAGIC.deviceCertificate,
            STRUCT_SIZE.deviceCertificate,
            SIGNED_SIZE.deviceCertificate
        );
        const authLevel = certificateBytes[5];
        if (authLevel < 1 || authLevel > 3) {
            throw new DeviceAuthV2Error(
                'INVALID_DEVICE_CERTIFICATE',
                'device certificate auth level is invalid',
                401
            );
        }
        requireZero(
            certificateBytes.subarray(129, 144),
            'deviceCertificate.reserved'
        );
        verifyP256(
            this.caPublicKey,
            parsed.signedBytes,
            parsed.signature,
            'INVALID_DEVICE_CERTIFICATE_SIGNATURE'
        );
        const serial = certificateBytes.subarray(8, 24);
        const deviceIdBytes = certificateBytes.subarray(24, 40);
        const hardwareVersionCode = certificateBytes.readUInt32LE(40);
        const issuedAt = certificateBytes.readUInt32LE(44);
        if (isAllZero(serial) || (hardwareVersionCode & 0xff000000) !== 0) {
            throw new DeviceAuthV2Error(
                'INVALID_DEVICE_CERTIFICATE',
                'certificate serial or hardware version is invalid',
                401
            );
        }
        const publicKey = normalizeP256PublicKey(
            certificateBytes.subarray(48, 113),
            'deviceCertificate.publicKey'
        );
        const derivedDeviceIdBytes = sha256(publicKey.raw)
            .subarray(0, 16)
        if (!safeEqual(derivedDeviceIdBytes, deviceIdBytes)) {
            throw new DeviceAuthV2Error(
                'DEVICE_ID_MISMATCH',
                'deviceId is not derived from the certified public key',
                401
            );
        }
        const nowSeconds = Math.floor((context.now || Date.now)() / 1000);
        if (issuedAt > nowSeconds + 60) {
            throw new DeviceAuthV2Error(
                'DEVICE_CERTIFICATE_NOT_YET_VALID',
                'device certificate issue time is in the future',
                401
            );
        }
        return {
            deviceId: deviceIdBytes.toString('hex').toUpperCase(),
            serialNumber: encodeBase64Url(serial),
            hardwareVersion: hardwareVersionFromCode(hardwareVersionCode),
            hardwareVersionCode,
            authLevel,
            issuedAt,
            devicePublicKey: publicKey.key,
            publicKeyRaw: publicKey.raw,
            productionBatch: encodeBase64Url(
                certificateBytes.subarray(113, 129)
            ),
            certificateFingerprint: sha256(certificateBytes).toString('hex'),
            certificateBytes: Buffer.from(certificateBytes)
        };
    }
}

class BinaryBootAttestationVerifier {
    verify(attestationBytes, context) {
        const parsed = parseSignedStructure(
            attestationBytes,
            'boot_attestation',
            MAGIC.bootAttestation,
            STRUCT_SIZE.bootAttestation,
            SIGNED_SIZE.bootAttestation,
            8
        );
        requireZero(
            attestationBytes.subarray(5, 8),
            'bootAttestation.reserved0'
        );
        requireZero(
            attestationBytes.subarray(10, 12),
            'bootAttestation.reserved1'
        );
        const deviceIdBytes = attestationBytes.subarray(12, 28);
        const bootNonce = attestationBytes.subarray(28, 60);
        const bootPublicKey = normalizeP256PublicKey(
            attestationBytes.subarray(60, 125),
            'bootAttestation.bootPublicKey'
        );
        if (deviceIdBytes.toString('hex').toUpperCase() !==
            context.identity.deviceId) {
            throw new DeviceAuthV2Error(
                'ATTESTATION_DEVICE_MISMATCH',
                'boot attestation belongs to a different device',
                401
            );
        }
        if (isAllZero(bootNonce) ||
            isAllZero(attestationBytes.subarray(125, 157))) {
            throw new DeviceAuthV2Error(
                'INVALID_BOOT_ATTESTATION',
                'boot nonce and firmware measurement must be nonzero',
                401
            );
        }
        verifyP256(
            context.identity.devicePublicKey,
            parsed.signedBytes,
            parsed.signature,
            'INVALID_BOOT_ATTESTATION_SIGNATURE'
        );
        return {
            deviceId: context.identity.deviceId,
            bootNonce: Buffer.from(bootNonce),
            firmwareMeasurement: attestationBytes
                .subarray(125, 157)
                .toString('hex'),
            securityVersion: attestationBytes.readUInt32LE(157),
            bootloaderVersion: attestationBytes.readUInt32LE(161),
            bootPublicKey: bootPublicKey.key,
            bootPublicKeyRaw: bootPublicKey.raw
        };
    }
}

function parseAndVerifyAttestationTranscript(bytes, context) {
    const parsed = parseSignedStructure(
        bytes,
        'attestation_transcript',
        MAGIC.attestationTranscript,
        STRUCT_SIZE.attestationTranscript,
        SIGNED_SIZE.attestationTranscript
    );
    if (bytes[5] !== 1) {
        throw new DeviceAuthV2Error(
            'UNSUPPORTED_WEBHID_PROTOCOL',
            'attestation transcript protocol version is unsupported',
            401
        );
    }
    const transcript = {
        challengeId: bytes.subarray(8, 24),
        serverNonce: bytes.subarray(24, 56),
        webhidSessionId: bytes.subarray(56, 72),
        requestedScopes: bytes.readUInt32LE(72),
        deviceId: bytes.subarray(76, 92),
        bootNonce: bytes.subarray(92, 124),
        browserPublicKey: bytes.subarray(124, 189),
        deviceEphemeralPublicKey: bytes.subarray(189, 254),
        firmwareHash: bytes.subarray(254, 286),
        securityVersion: bytes.readUInt32LE(286)
    };
    const bindingsMatch =
        safeEqual(
            transcript.challengeId,
            decodeBase64Url(context.challenge.challengeId, 'challengeId', 16)
        ) &&
        safeEqual(transcript.serverNonce, context.challenge.nonce) &&
        !isAllZero(transcript.webhidSessionId) &&
        transcript.requestedScopes === context.challenge.scopeMask &&
        safeEqual(
            transcript.deviceId,
            Buffer.from(context.identity.deviceId, 'hex')
        ) &&
        safeEqual(transcript.bootNonce, context.attestation.bootNonce) &&
        safeEqual(
            transcript.browserPublicKey,
            context.browserPublicKey
        ) &&
        safeEqual(
            transcript.deviceEphemeralPublicKey,
            context.deviceEphemeralPublicKey
        ) &&
        safeEqual(
            transcript.firmwareHash,
            Buffer.from(context.attestation.firmwareMeasurement, 'hex')
        ) &&
        transcript.securityVersion === context.attestation.securityVersion;
    if (!bindingsMatch) {
        throw new DeviceAuthV2Error(
            'ATTESTATION_BINDING_MISMATCH',
            'attestation transcript does not match the issued challenge',
            401
        );
    }
    const deviceKey = normalizeP256PublicKey(
        transcript.deviceEphemeralPublicKey,
        'attestationTranscript.deviceEphemeralPublicKey'
    );
    verifyP256(
        context.attestation.bootPublicKey,
        parsed.signedBytes,
        parsed.signature,
        'INVALID_SESSION_PROOF'
    );
    return {
        ...transcript,
        deviceEphemeralPublicKey: deviceKey.raw
    };
}

class BinaryP256PermitSigner {
    constructor(privateKey, options = {}) {
        if (!privateKey) {
            throw new Error('WebConfig authorization private key is required');
        }
        this.privateKey = privateKey.type === 'private'
            ? privateKey
            : crypto.createPrivateKey(privateKey);
        if (this.privateKey.asymmetricKeyType !== 'ec' ||
            (this.privateKey.asymmetricKeyDetails || {}).namedCurve !==
                'prime256v1') {
            throw new Error('WebConfig authorization key must be P-256');
        }
        this.signingKeySlot = Number.isInteger(options.signingKeySlot) &&
            options.signingKeySlot >= 0 &&
            options.signingKeySlot <= 255
            ? options.signingKeySlot
            : 0;
    }

    sign(claims) {
        const permit = Buffer.alloc(STRUCT_SIZE.sessionPermit);
        permit.writeUInt32LE(MAGIC.sessionPermit, 0);
        permit[4] = 1;
        permit[5] = this.signingKeySlot;
        permit.writeUInt16LE(SIGNED_SIZE.sessionPermit, 6);
        claims.permitId.copy(permit, 8);
        claims.sessionId.copy(permit, 24);
        claims.deviceId.copy(permit, 40);
        claims.bootNonce.copy(permit, 56);
        claims.browserPublicKeyHash.copy(permit, 88);
        claims.devicePublicKeyHash.copy(permit, 120);
        permit.writeUInt32LE(claims.scopeMask, 152);
        permit.writeUInt32LE(claims.maxDurationMs, 156);
        permit.writeUInt32LE(claims.issuedAt, 160);
        permit.writeUInt32LE(claims.expiresAt, 164);
        permit.writeUInt32LE(claims.policyVersion, 168);
        const signature = crypto.sign(
            'sha256',
            permit.subarray(0, SIGNED_SIZE.sessionPermit),
            { key: this.privateKey, dsaEncoding: 'ieee-p1363' }
        );
        signature.copy(permit, SIGNED_SIZE.sessionPermit);
        return permit;
    }
}

class StorageDevicePolicy {
    constructor(storageManager) {
        this.storageManager = storageManager;
    }

    get(deviceId) {
        return this.storageManager.findDevice(deviceId);
    }

    check(identity, attestation) {
        const record = this.get(identity.deviceId);
        if (!record || record.authVersion !== 2) {
            throw new DeviceAuthV2Error(
                'DEVICE_NOT_ENROLLED',
                'device is not enrolled for V2 authentication',
                401
            );
        }
        if (record.status !== 'active' || record.revokedAt) {
            throw new DeviceAuthV2Error(
                'DEVICE_REVOKED',
                'device has been revoked',
                403
            );
        }
        if (record.certificateFingerprint !==
            identity.certificateFingerprint ||
            record.certificateSerial !== identity.serialNumber ||
            record.hardwareVersion !== identity.hardwareVersion ||
            record.authLevel !== identity.authLevel) {
            throw new DeviceAuthV2Error(
                'DEVICE_ENROLLMENT_MISMATCH',
                'certificate does not match the enrolled device',
                401
            );
        }
        const minimum = Number.isSafeInteger(record.minSecurityVersion)
            ? record.minSecurityVersion
            : 0;
        if (attestation.securityVersion < minimum) {
            throw new DeviceAuthV2Error(
                'FIRMWARE_ROLLBACK_BLOCKED',
                'firmware security version is below policy',
                403
            );
        }
        const allowlist = Array.isArray(record.allowedFirmwareMeasurements)
            ? record.allowedFirmwareMeasurements
            : [];
        if (allowlist.length > 0 &&
            !allowlist.includes(attestation.firmwareMeasurement)) {
            throw new DeviceAuthV2Error(
                'FIRMWARE_NOT_TRUSTED',
                'firmware measurement is not permitted',
                403
            );
        }
        return record;
    }
}

class DeviceAuthV2Service {
    constructor(options) {
        this.certificateVerifier = options.certificateVerifier;
        this.attestationVerifier = options.attestationVerifier;
        this.permitSigner = options.permitSigner;
        this.devicePolicy = options.devicePolicy;
        this.challengeStore = options.challengeStore ||
            new MemoryChallengeStore(options);
        this.tokenStore = options.tokenStore || new OpaqueTokenStore(options);
        this.challengeLimiter = options.challengeLimiter || null;
        this.verifyLimiter = options.verifyLimiter || null;
        this.now = options.now || Date.now;
    }

    assertReady() {
        if (!this.isReady()) {
            throw new DeviceAuthV2Error(
                'AUTH_V2_NOT_CONFIGURED',
                'V2 device authentication is not configured',
                503
            );
        }
    }

    isReady() {
        return Boolean(
            this.certificateVerifier &&
            this.attestationVerifier &&
            this.permitSigner &&
            this.devicePolicy
        );
    }

    issueChallenge({
        origin,
        protocol,
        requestedScopes,
        browserPublicKey,
        remoteAddress
    }) {
        this.assertReady();
        if (typeof origin !== 'string' || origin.length === 0) {
            throw new DeviceAuthV2Error(
                'ORIGIN_REQUIRED',
                'a trusted browser origin is required',
                400
            );
        }
        if (protocol !== 'hbox-webhid-v1') {
            throw new DeviceAuthV2Error(
                'UNSUPPORTED_AUTH_PROTOCOL',
                'only hbox-webhid-v1 is supported',
                400
            );
        }
        const scopes = normalizeScopes(requestedScopes);
        const browserKey = browserPublicKey
            ? normalizeP256PublicKey(browserPublicKey, 'browserPublicKey')
            : null;
        const issued = this.challengeStore.issue({
            origin,
            remoteAddress,
            scopes,
            scopeMask: scopesToMask(scopes),
            browserPublicKey: browserKey && browserKey.raw
        });
        const format = record => ({
            challengeId: record.challengeId,
            nonce: encodeBase64(record.nonce),
            expiresAt: record.expiresAt,
            expiresIn: Math.floor(
                (record.expiresAt - this.now()) / 1000
            ),
            requestedScopes: record.scopes
        });
        if (issued && typeof issued.then === 'function') {
            return issued.then(format);
        }
        return format(issued);
    }

    async verifyAndCreateSession(request, context = {}) {
        this.assertReady();
        if (typeof request.challengeId !== 'string') {
            throw new DeviceAuthV2Error(
                'CHALLENGE_REQUIRED',
                'challengeId is required',
                400
            );
        }
        /*
         * Consume before parsing signatures. Invalid attempts burn the
         * challenge, removing replay and verification-oracle behavior.
         */
        const challenge = await this.challengeStore.consume(
            request.challengeId
        );
        if (typeof context.origin !== 'string' ||
            context.origin !== challenge.origin) {
            throw new DeviceAuthV2Error(
                'CHALLENGE_ORIGIN_MISMATCH',
                'verification origin does not match the issued challenge',
                401
            );
        }
        const requestedScopes = normalizeScopes(
            request.requestedScopes || challenge.scopes
        );
        if (requestedScopes.length !== challenge.scopes.length ||
            requestedScopes.some((scope, index) =>
                scope !== challenge.scopes[index])) {
            throw new DeviceAuthV2Error(
                'CHALLENGE_SCOPE_MISMATCH',
                'requested scopes do not match the issued challenge',
                401
            );
        }
        if (request.challengeNonce &&
            !safeEqual(
                decodeWireBase64(
                    request.challengeNonce,
                    'challengeNonce',
                    32
                ),
                challenge.nonce
            )) {
            throw new DeviceAuthV2Error(
                'CHALLENGE_NONCE_MISMATCH',
                'challenge nonce does not match the issued challenge',
                401
            );
        }
        const deviceProof = request.deviceAttestation || request;
        const browserKey = normalizeP256PublicKey(
            request.browserEphemeralPublicKey || request.browserPublicKey,
            'browserEphemeralPublicKey'
        );
        if (challenge.browserPublicKey &&
            !safeEqual(browserKey.raw, challenge.browserPublicKey)) {
            throw new DeviceAuthV2Error(
                'CHALLENGE_BINDING_MISMATCH',
                'browser key does not match the issued challenge',
                401
            );
        }
        const identity = await this.certificateVerifier.verify(
            decodeWireBase64(
                deviceProof.certificate || deviceProof.deviceCertificate,
                'deviceCertificate',
                STRUCT_SIZE.deviceCertificate
            ),
            { now: this.now }
        );
        const attestation = await this.attestationVerifier.verify(
            decodeWireBase64(
                deviceProof.bootAttestation,
                'bootAttestation',
                STRUCT_SIZE.bootAttestation
            ),
            { identity, now: this.now }
        );
        const deviceRecord = await this.devicePolicy.check(
            identity,
            attestation
        );
        const deviceEphemeralKey = normalizeP256PublicKey(
            deviceProof.deviceEphemeralPublicKey,
            'deviceEphemeralPublicKey'
        );
        const transcript = parseAndVerifyAttestationTranscript(
            decodeWireBase64(
                /*
                 * Compatibility name locked by the first browser client:
                 * "signature" carries the complete 354-byte signed
                 * hbox_attestation_transcript_v1_t, not a naked signature.
                 */
                deviceProof.signature || request.attestationTranscript,
                'deviceAttestation.signatureTranscript',
                STRUCT_SIZE.attestationTranscript
            ),
            {
                challenge,
                identity,
                attestation,
                browserPublicKey: browserKey.raw,
                deviceEphemeralPublicKey: deviceEphemeralKey.raw
            }
        );
        if (typeof deviceProof.deviceId !== 'string' ||
            deviceProof.deviceId.toUpperCase() !== identity.deviceId ||
            typeof deviceProof.bootNonce !== 'string' ||
            !safeEqual(
                decodeWireBase64(deviceProof.bootNonce, 'bootNonce', 32),
                attestation.bootNonce
            ) ||
            typeof deviceProof.firmwareMeasurement !== 'string' ||
            !/^[0-9a-fA-F]{64}$/.test(
                deviceProof.firmwareMeasurement
            ) ||
            deviceProof.firmwareMeasurement.toLowerCase() !==
                attestation.firmwareMeasurement ||
            deviceProof.hardwareVersion !== identity.hardwareVersion ||
            typeof deviceProof.firmwareVersion !== 'string' ||
            !/^\d+\.\d+\.\d+$/.test(deviceProof.firmwareVersion)) {
            throw new DeviceAuthV2Error(
                'DEVICE_PROOF_FIELD_MISMATCH',
                'device proof summary does not match its signed structures',
                401
            );
        }

        const sessionId = transcript.webhidSessionId;
        const issuedAt = Math.floor(this.now() / 1000);
        const ttlSeconds = Math.floor(this.tokenStore.ttlMs / 1000);
        const permitClaims = {
            permitId: crypto.randomBytes(16),
            sessionId,
            deviceId: Buffer.from(identity.deviceId, 'hex'),
            bootNonce: attestation.bootNonce,
            browserPublicKeyHash: sha256(browserKey.raw),
            devicePublicKeyHash: sha256(
                transcript.deviceEphemeralPublicKey
            ),
            scopeMask: challenge.scopeMask,
            maxDurationMs: this.tokenStore.ttlMs,
            issuedAt,
            expiresAt: issuedAt + ttlSeconds,
            policyVersion: Number.isSafeInteger(deviceRecord.policyVersion)
                ? deviceRecord.policyVersion
                : 1
        };
        const permit = await this.permitSigner.sign(permitClaims);
        /*
         * Both browser and STM32 can derive this salt without adding another
         * field to the fixed permit: STM32 hashes the installed permit bytes.
         */
        const sessionSalt = sha256(permit);
        const issued = await this.tokenStore.issue({
            sessionId: encodeBase64Url(sessionId),
            deviceId: identity.deviceId,
            deviceName: deviceRecord.deviceName,
            hardwareVersion: identity.hardwareVersion,
            scopes: challenge.scopes,
            scopeMask: challenge.scopeMask,
            policyVersion: permitClaims.policyVersion,
            origin: challenge.origin
        });
        return {
            apiToken: issued.token,
            expiresInMs: issued.record.expiresAt - this.now(),
            sessionId: encodeBase64Url(sessionId),
            deviceSessionPermit: encodeBase64(permit),
            sessionSalt: encodeBase64(sessionSalt),
            scopes: challenge.scopes,
            // Transitional aliases for non-browser integration clients.
            expiresIn: Math.floor(
                (issued.record.expiresAt - this.now()) / 1000
            ),
            grantedScopes: challenge.scopes,
            permit: encodeBase64Url(permit)
        };
    }

    requireSession(requiredScopes = []) {
        const normalizedRequired = requiredScopes.length
            ? normalizeScopes(requiredScopes)
            : [];
        return async (req, res, next) => {
            try {
                const header = req.get('authorization');
                if (!header || !/^Bearer [A-Za-z0-9_-]+$/.test(header)) {
                    throw new DeviceAuthV2Error(
                        'DEVICE_SESSION_REQUIRED',
                        'a V2 Bearer device session is required',
                        401
                    );
                }
                const token = header.substring('Bearer '.length);
                const session = await this.tokenStore.resolve(token);
                if (!session) {
                    throw new DeviceAuthV2Error(
                        'DEVICE_SESSION_INVALID',
                        'device session is invalid or expired',
                        401
                    );
                }
                const currentDevice = this.devicePolicy &&
                    await this.devicePolicy.get(session.deviceId);
                if (!currentDevice ||
                    currentDevice.status !== 'active' ||
                    currentDevice.revokedAt ||
                    (currentDevice.policyVersion || 1) !==
                        session.policyVersion) {
                    await this.tokenStore.revoke(token);
                    throw new DeviceAuthV2Error(
                        'DEVICE_SESSION_REVOKED',
                        'device session was revoked by policy',
                        403
                    );
                }
                if (normalizedRequired.some(scope =>
                    !session.scopes.includes(scope))) {
                    throw new DeviceAuthV2Error(
                        'DEVICE_SCOPE_DENIED',
                        'device session does not grant the required scope',
                        403
                    );
                }
                req.deviceSession = session;
                next();
            } catch (error) {
                sendDeviceAuthError(res, error);
            }
        };
    }
}

function sendDeviceAuthError(res, error) {
    const known = error instanceof DeviceAuthV2Error;
    const status = known ? error.status : 500;
    if (known && Number.isSafeInteger(error.retryAfterSeconds)) {
        res.set('Retry-After', String(error.retryAfterSeconds));
    }
    return res.status(status).json({
        success: false,
        error: known ? error.code : 'AUTH_V2_SERVER_ERROR',
        message: known ? error.message : 'V2 device authentication failed'
    });
}

function initDeviceAuthV2Routes(
    app,
    service,
    storageManager,
    requireAdminAuth,
    options = {}
) {
    const challengeLimiter = options.challengeLimiter ||
        service.challengeLimiter ||
        new SlidingWindowRateLimiter({
            maxAttempts: 20,
            windowMs: 60 * 1000
        });
    const verifyLimiter = options.verifyLimiter ||
        service.verifyLimiter ||
        new SlidingWindowRateLimiter({
            maxAttempts: 30,
            windowMs: 60 * 1000
        });

    app.post('/api/v2/device-auth/challenges', async (req, res) => {
        try {
            await challengeLimiter.check(req.ip);
            const data = await service.issueChallenge({
                origin: req.get('origin'),
                protocol: req.body && req.body.protocol,
                requestedScopes: req.body && req.body.requestedScopes,
                browserPublicKey: req.body && req.body.browserPublicKey,
                remoteAddress: req.ip
            });
            res.set('Cache-Control', 'no-store');
            res.status(201).json(data);
        } catch (error) {
            sendDeviceAuthError(res, error);
        }
    });

    app.post('/api/v2/device-auth/verify', async (req, res) => {
        try {
            await verifyLimiter.check(req.ip);
            const data = await service.verifyAndCreateSession(
                req.body || {},
                { origin: req.get('origin') }
            );
            res.set('Cache-Control', 'no-store');
            res.json(data);
        } catch (error) {
            sendDeviceAuthError(res, error);
        }
    });

    app.get(
        '/api/v2/device-auth/session',
        service.requireSession(['config.read']),
        async (req, res) => {
            res.set('Cache-Control', 'no-store');
            res.json({
                success: true,
                data: {
                    sessionId: req.deviceSession.sessionId,
                    deviceId: req.deviceSession.deviceId,
                    hardwareVersion: req.deviceSession.hardwareVersion,
                    scopes: req.deviceSession.scopes
                }
            });
        }
    );

    app.post(
        '/api/v2/devices',
        requireAdminAuth(),
        async (req, res) => {
            try {
                service.assertReady();
                const certificateBytes = decodeWireBase64(
                    req.body && req.body.deviceCertificate,
                    'deviceCertificate',
                    STRUCT_SIZE.deviceCertificate
                );
                const identity = await service.certificateVerifier.verify(
                    certificateBytes,
                    { now: service.now }
                );
                const minSecurityVersion = normalizeSecurityVersion(
                    req.body.minSecurityVersion === undefined
                        ? 0
                        : req.body.minSecurityVersion
                );
                const allowedFirmwareMeasurements =
                    normalizeFirmwareMeasurements(
                        req.body.allowedFirmwareMeasurements || []
                    );
                const result = storageManager.addV2Device({
                    deviceId: identity.deviceId,
                    deviceName: req.body.deviceName ||
                        `HBox-${identity.deviceId.substring(0, 8)}`,
                    certificateSerial: identity.serialNumber,
                    certificateFingerprint: identity.certificateFingerprint,
                    hardwareVersion: identity.hardwareVersion,
                    authLevel: identity.authLevel,
                    minSecurityVersion,
                    allowedFirmwareMeasurements,
                    registeredBy: req.authenticatedAdmin.username
                });
                if (result.conflict) {
                    return res.status(409).json({
                        success: false,
                        error: 'DEVICE_ENROLLMENT_CONFLICT',
                        message: 'deviceId is already enrolled with another certificate'
                    });
                }
                if (!result.success) {
                    throw new DeviceAuthV2Error(
                        'DEVICE_ENROLLMENT_STORAGE_FAILED',
                        'failed to persist V2 device enrollment',
                        500
                    );
                }
                res.status(result.existed ? 200 : 201).json({
                    success: true,
                    data: sanitizeDeviceRecord(result.device),
                    existed: result.existed
                });
            } catch (error) {
                sendDeviceAuthError(res, error);
            }
        }
    );

    app.put(
        '/api/v2/devices/:deviceId/policy',
        requireAdminAuth(),
        (req, res) => {
            try {
                const minSecurityVersion = normalizeSecurityVersion(
                    req.body.minSecurityVersion
                );
                const allowedFirmwareMeasurements =
                    normalizeFirmwareMeasurements(
                        req.body.allowedFirmwareMeasurements
                    );
                const currentDevice = storageManager.findDevice(
                    req.params.deviceId
                );
                if (currentDevice &&
                    Number.isSafeInteger(currentDevice.minSecurityVersion) &&
                    minSecurityVersion <
                        currentDevice.minSecurityVersion) {
                    throw new DeviceAuthV2Error(
                        'FIRMWARE_POLICY_ROLLBACK',
                        'minSecurityVersion cannot be decreased',
                        409
                    );
                }
                const device = storageManager.updateV2DevicePolicy(
                    req.params.deviceId,
                    {
                        minSecurityVersion,
                        allowedFirmwareMeasurements
                    }
                );
                if (!device) {
                    return res.status(404).json({
                        success: false,
                        error: 'DEVICE_NOT_FOUND',
                        message: 'V2 device was not found'
                    });
                }
                return res.json({
                    success: true,
                    data: sanitizeDeviceRecord(device)
                });
            } catch (error) {
                return sendDeviceAuthError(res, error);
            }
        }
    );

    app.post(
        '/api/v2/devices/:deviceId/revoke',
        requireAdminAuth(),
        (req, res) => {
            const device = storageManager.revokeV2Device(
                req.params.deviceId,
                req.body && req.body.reason,
                req.authenticatedAdmin.username
            );
            if (!device) {
                return res.status(404).json({
                    success: false,
                    error: 'DEVICE_NOT_FOUND',
                    message: 'V2 device was not found'
                });
            }
            return res.json({
                success: true,
                data: sanitizeDeviceRecord(device)
            });
        }
    );
}

function sanitizeDeviceRecord(device) {
    return {
        deviceId: device.deviceId,
        deviceName: device.deviceName,
        hardwareVersion: device.hardwareVersion,
        authVersion: device.authVersion,
        authLevel: device.authLevel,
        certificateSerial: device.certificateSerial,
        certificateFingerprint: device.certificateFingerprint,
        minSecurityVersion: device.minSecurityVersion,
        policyVersion: device.policyVersion,
        allowedFirmwareMeasurements: device.allowedFirmwareMeasurements,
        status: device.status,
        registerTime: device.registerTime,
        revokedAt: device.revokedAt || null,
        revocationReason: device.revocationReason || null
    };
}

function readPemFromEnvironment(
    valueName,
    fileName,
    fsModule,
    environment = process.env
) {
    if (environment[valueName]) {
        return environment[valueName].replace(/\\n/g, '\n');
    }
    if (environment[fileName]) {
        return fsModule.readFileSync(environment[fileName], 'utf8');
    }
    return null;
}

function adapterHasMethods(value, methods) {
    return value && methods.every(method =>
        typeof value[method] === 'function');
}

function validateProductionAdapter(dependencies) {
    if (!dependencies || typeof dependencies !== 'object' ||
        !adapterHasMethods(dependencies.permitSigner, ['sign']) ||
        !adapterHasMethods(
            dependencies.challengeStore, ['issue', 'consume']) ||
        !adapterHasMethods(
            dependencies.tokenStore, ['issue', 'resolve', 'revoke']) ||
        !adapterHasMethods(
            dependencies.devicePolicy, ['check', 'get']) ||
        !adapterHasMethods(dependencies.challengeLimiter, ['check']) ||
        !adapterHasMethods(dependencies.verifyLimiter, ['check'])) {
        throw new Error(
            'production adapter must provide KMS signer, shared stores, ' +
            'transactional device policy and shared rate limiters'
        );
    }
    if (!Number.isSafeInteger(dependencies.tokenStore.ttlMs) ||
        dependencies.tokenStore.ttlMs < 1 ||
        dependencies.tokenStore.ttlMs > SESSION_TTL_MS) {
        throw new Error(
            `production tokenStore.ttlMs must be a safe integer between ` +
            `1 and ${SESSION_TTL_MS}`
        );
    }
    return dependencies;
}

function loadProductionAdapter(environment, options, storageManager) {
    if (options.adapterDependencies) {
        return validateProductionAdapter(options.adapterDependencies);
    }
    const modulePath = String(
        environment.DEVICE_AUTH_V2_ADAPTER_MODULE || ''
    ).trim();
    if (!modulePath) {
        throw new Error(
            'DEVICE_AUTH_V2_ADAPTER_MODULE is required in production'
        );
    }
    const pathModule = options.path || require('path');
    if (!pathModule.isAbsolute(modulePath)) {
        throw new Error(
            'DEVICE_AUTH_V2_ADAPTER_MODULE must be an absolute path'
        );
    }
    const loadModule = options.requireModule || require;
    const adapter = loadModule(modulePath);
    if (!adapter ||
        typeof adapter.createDeviceAuthV2Dependencies !== 'function') {
        throw new Error(
            'production adapter must export ' +
            'createDeviceAuthV2Dependencies()'
        );
    }
    const dependencies = adapter.createDeviceAuthV2Dependencies({
        storageManager,
        environment
    });
    if (dependencies && typeof dependencies.then === 'function') {
        throw new Error(
            'production adapter factory must initialize synchronously'
        );
    }
    return validateProductionAdapter(dependencies);
}

function createDeviceAuthV2FromEnvironment(storageManager, options = {}) {
    const fsModule = options.fs || require('fs');
    const environment = options.environment || process.env;
    const caPublicKey = readPemFromEnvironment(
        'DEVICE_CA_PUBLIC_KEY_PEM',
        'DEVICE_CA_PUBLIC_KEY_FILE',
        fsModule,
        environment
    );
    const production = environment.NODE_ENV === 'production';
    let dependencies = null;
    if (production) {
        try {
            dependencies = loadProductionAdapter(
                environment, options, storageManager
            );
        } catch (error) {
            console.error(
                'V2 production authentication adapter rejected:',
                error.message
            );
        }
    }
    const permitPrivateKey = production
        ? null
        : readPemFromEnvironment(
            'WEB_CONFIG_AUTH_PRIVATE_KEY_PEM',
            'WEB_CONFIG_AUTH_PRIVATE_KEY_FILE',
            fsModule,
            environment
        );
    if (!caPublicKey ||
        (!dependencies && !permitPrivateKey)) {
        console.warn(
            'V2 device authentication is fail-closed: configure ' +
            (production
                ? 'DEVICE_CA_PUBLIC_KEY_* and the production KMS/store adapter'
                : 'DEVICE_CA_PUBLIC_KEY_* and WEB_CONFIG_AUTH_PRIVATE_KEY_*')
        );
        return new DeviceAuthV2Service({
            certificateVerifier: null,
            attestationVerifier: null,
            permitSigner: null,
            devicePolicy: new StorageDevicePolicy(storageManager)
        });
    }
    return new DeviceAuthV2Service({
        certificateVerifier:
            new BinaryDeviceCertificateVerifier(caPublicKey),
        attestationVerifier: new BinaryBootAttestationVerifier(),
        permitSigner: dependencies
            ? dependencies.permitSigner
            : new BinaryP256PermitSigner(permitPrivateKey, {
                signingKeySlot: Number.parseInt(
                    environment.WEB_CONFIG_AUTH_KEY_SLOT || '0',
                    10
                )
            }),
        devicePolicy: dependencies
            ? dependencies.devicePolicy
            : new StorageDevicePolicy(storageManager),
        challengeStore: dependencies &&
            dependencies.challengeStore,
        tokenStore: dependencies && dependencies.tokenStore,
        challengeLimiter: dependencies &&
            dependencies.challengeLimiter,
        verifyLimiter: dependencies && dependencies.verifyLimiter
    });
}

module.exports = {
    CHALLENGE_TTL_MS,
    SESSION_TTL_MS,
    DEVICE_AUTH_SCOPES,
    DeviceAuthV2Error,
    SlidingWindowRateLimiter,
    MemoryChallengeStore,
    OpaqueTokenStore,
    BinaryDeviceCertificateVerifier,
    BinaryBootAttestationVerifier,
    BinaryP256PermitSigner,
    StorageDevicePolicy,
    DeviceAuthV2Service,
    encodeBase64Url,
    encodeBase64,
    decodeBase64Url,
    decodeWireBase64,
    normalizeP256PublicKey,
    normalizeFirmwareMeasurements,
    normalizeSecurityVersion,
    parseAndVerifyAttestationTranscript,
    initDeviceAuthV2Routes,
    sendDeviceAuthError,
    validateProductionAdapter,
    loadProductionAdapter,
    createDeviceAuthV2FromEnvironment
};
