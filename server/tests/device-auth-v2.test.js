'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('fs-extra');
const http = require('node:http');
const os = require('node:os');
const path = require('node:path');
const test = require('node:test');

const express = require('express');
const cors = require('cors');

const { FirmwareStorage } = require('../src/firmware');
const { initAllRoutes } = require('../src/action');
const {
    DEVICE_AUTH_SCOPES,
    SlidingWindowRateLimiter,
    MemoryChallengeStore,
    OpaqueTokenStore,
    BinaryDeviceCertificateVerifier,
    BinaryBootAttestationVerifier,
    BinaryP256PermitSigner,
    StorageDevicePolicy,
    DeviceAuthV2Service,
    encodeBase64Url,
    decodeBase64Url,
    initDeviceAuthV2Routes,
    validateProductionAdapter,
    createDeviceAuthV2FromEnvironment
} = require('../src/device-auth-v2');
const {
    createExactCorsOptions,
    securityHeaders
} = require('../src/http-security');
const {
    LegacyDownloadTicketStore,
    initLegacyDownloadRoute,
    createLegacyDownloadUrl
} = require('../src/download-access');
const {
    generateDeviceSignature,
    validateDeviceAuth
} = require('../src/device-auth');

const MAGIC = Object.freeze({
    certificate: 0x31434448,
    attestation: 0x31414248,
    transcript: 0x31544148,
    permit: 0x31505348
});

function generateP256KeyPair() {
    return crypto.generateKeyPairSync('ec', {
        namedCurve: 'prime256v1'
    });
}

function rawPublicKey(publicKey) {
    const jwk = publicKey.export({ format: 'jwk' });
    return Buffer.concat([
        Buffer.from([0x04]),
        decodeBase64Url(jwk.x, 'jwk.x', 32),
        decodeBase64Url(jwk.y, 'jwk.y', 32)
    ]);
}

function signP256(privateKey, bytes) {
    return crypto.sign(
        'sha256',
        bytes,
        { key: privateKey, dsaEncoding: 'ieee-p1363' }
    );
}

function makeCertificate(caPrivateKey, devicePublicKey, nowSeconds) {
    const devicePublic = rawPublicKey(devicePublicKey);
    const deviceId = crypto.createHash('sha256')
        .update(devicePublic)
        .digest()
        .subarray(0, 16);
    const certificate = Buffer.alloc(208);
    certificate.writeUInt32LE(MAGIC.certificate, 0);
    certificate[4] = 1;
    certificate[5] = 1;
    certificate.writeUInt16LE(144, 6);
    Buffer.alloc(16, 0x11).copy(certificate, 8);
    deviceId.copy(certificate, 24);
    certificate.writeUInt32LE(0x00020000, 40);
    certificate.writeUInt32LE(nowSeconds - 10, 44);
    devicePublic.copy(certificate, 48);
    Buffer.alloc(16, 0x22).copy(certificate, 113);
    signP256(caPrivateKey, certificate.subarray(0, 144))
        .copy(certificate, 144);
    return { certificate, deviceId };
}

function makeBootAttestation(
    devicePrivateKey,
    bootPublicKey,
    deviceId,
    firmwareHash,
    securityVersion
) {
    const attestation = Buffer.alloc(229);
    const bootNonce = Buffer.alloc(32, 0x33);
    attestation.writeUInt32LE(MAGIC.attestation, 0);
    attestation[4] = 1;
    attestation.writeUInt16LE(165, 8);
    deviceId.copy(attestation, 12);
    bootNonce.copy(attestation, 28);
    rawPublicKey(bootPublicKey).copy(attestation, 60);
    firmwareHash.copy(attestation, 125);
    attestation.writeUInt32LE(securityVersion, 157);
    attestation.writeUInt32LE(7, 161);
    signP256(devicePrivateKey, attestation.subarray(0, 165))
        .copy(attestation, 165);
    return { attestation, bootNonce };
}

function makeTranscript(fixture, challenge, options = {}) {
    const transcript = Buffer.alloc(354);
    transcript.writeUInt32LE(MAGIC.transcript, 0);
    transcript[4] = 1;
    transcript[5] = 1;
    transcript.writeUInt16LE(290, 6);
    decodeBase64Url(challenge.challengeId, 'challengeId', 16)
        .copy(transcript, 8);
    Buffer.from(challenge.nonce, 'base64').copy(transcript, 24);
    fixture.webhidSessionId.copy(transcript, 56);
    transcript.writeUInt32LE(options.scopeMask || fixture.scopeMask, 72);
    fixture.deviceId.copy(transcript, 76);
    fixture.bootNonce.copy(transcript, 92);
    fixture.browserPublic.copy(transcript, 124);
    fixture.deviceEphemeralPublic.copy(transcript, 189);
    fixture.firmwareHash.copy(transcript, 254);
    transcript.writeUInt32LE(fixture.securityVersion, 286);
    signP256(fixture.bootKeys.privateKey, transcript.subarray(0, 290))
        .copy(transcript, 290);
    return transcript;
}

function createFixture() {
    const tempDir = fs.mkdtempSync(
        path.join(os.tmpdir(), 'hbox-device-auth-v2-')
    );
    const clock = { value: 1800000000000 };
    const now = () => clock.value;
    const caKeys = generateP256KeyPair();
    const deviceKeys = generateP256KeyPair();
    const bootKeys = generateP256KeyPair();
    const browserKeys = generateP256KeyPair();
    const deviceEphemeralKeys = generateP256KeyPair();
    const permitKeys = generateP256KeyPair();
    const firmwareHash = Buffer.alloc(32, 0x44);
    const securityVersion = 3;
    const certificateResult = makeCertificate(
        caKeys.privateKey,
        deviceKeys.publicKey,
        Math.floor(now() / 1000)
    );
    const bootResult = makeBootAttestation(
        deviceKeys.privateKey,
        bootKeys.publicKey,
        certificateResult.deviceId,
        firmwareHash,
        securityVersion
    );
    const storage = new FirmwareStorage(
        path.join(tempDir, 'firmwares.json'),
        path.join(tempDir, 'uploads')
    );
    fs.ensureDirSync(storage.uploadDir);
    const certificateVerifier = new BinaryDeviceCertificateVerifier(
        caKeys.publicKey
    );
    const identity = certificateVerifier.verify(
        certificateResult.certificate,
        { now }
    );
    const enrollment = storage.addV2Device({
        deviceId: identity.deviceId,
        deviceName: 'Test HBox',
        certificateSerial: identity.serialNumber,
        certificateFingerprint: identity.certificateFingerprint,
        hardwareVersion: identity.hardwareVersion,
        authLevel: identity.authLevel,
        minSecurityVersion: 3,
        allowedFirmwareMeasurements: [firmwareHash.toString('hex')],
        registeredBy: 'test'
    });
    assert.equal(enrollment.success, true);
    const tokenStore = new OpaqueTokenStore({ now });
    const service = new DeviceAuthV2Service({
        certificateVerifier,
        attestationVerifier: new BinaryBootAttestationVerifier(),
        permitSigner: new BinaryP256PermitSigner(
            permitKeys.privateKey,
            { signingKeySlot: 1 }
        ),
        devicePolicy: new StorageDevicePolicy(storage),
        challengeStore: new MemoryChallengeStore({ now }),
        tokenStore,
        now
    });
    const scopes = ['config.read', 'firmware.update'];
    return {
        tempDir,
        clock,
        now,
        storage,
        service,
        tokenStore,
        caKeys,
        deviceKeys,
        bootKeys,
        browserKeys,
        deviceEphemeralKeys,
        permitKeys,
        certificate: certificateResult.certificate,
        bootAttestation: bootResult.attestation,
        deviceId: certificateResult.deviceId,
        bootNonce: bootResult.bootNonce,
        firmwareHash,
        securityVersion,
        scopes,
        scopeMask: DEVICE_AUTH_SCOPES['config.read'] |
            DEVICE_AUTH_SCOPES['firmware.update'],
        browserPublic: rawPublicKey(browserKeys.publicKey),
        deviceEphemeralPublic:
            rawPublicKey(deviceEphemeralKeys.publicKey),
        webhidSessionId: Buffer.alloc(16, 0x55)
    };
}

function issueChallenge(fixture, scopes = fixture.scopes) {
    return fixture.service.issueChallenge({
        origin: 'https://firmware.st-dash.com',
        protocol: 'hbox-webhid-v1',
        requestedScopes: scopes,
        remoteAddress: '127.0.0.1'
    });
}

function makeVerifyRequest(
    fixture,
    challenge,
    scopes = fixture.scopes
) {
    const scopeMask = scopes.reduce(
        (mask, scope) => mask | DEVICE_AUTH_SCOPES[scope],
        0
    );
    return {
        challengeId: challenge.challengeId,
        challengeNonce: challenge.nonce,
        browserEphemeralPublicKey:
            fixture.browserPublic.toString('base64'),
        requestedScopes: scopes,
        deviceAttestation: {
            deviceId: fixture.deviceId.toString('hex').toUpperCase(),
            certificate: fixture.certificate.toString('base64'),
            bootAttestation: fixture.bootAttestation.toString('base64'),
            bootNonce: fixture.bootNonce.toString('base64'),
            deviceEphemeralPublicKey:
                fixture.deviceEphemeralPublic.toString('base64'),
            firmwareMeasurement: fixture.firmwareHash.toString('hex'),
            hardwareVersion: '2.0.0',
            firmwareVersion: '1.0.0',
            // Compatibility field contains the complete 354-byte transcript.
            signature: makeTranscript(
                fixture,
                challenge,
                { scopeMask }
            ).toString('base64')
        }
    };
}

function verifySession(fixture, request) {
    return fixture.service.verifyAndCreateSession(request, {
        origin: 'https://firmware.st-dash.com'
    });
}

function verifyPermit(fixture, encodedPermit) {
    const permit = Buffer.from(encodedPermit, 'base64');
    assert.equal(permit.length, 236);
    assert.equal(permit.readUInt32LE(0), MAGIC.permit);
    assert.equal(permit[4], 1);
    assert.equal(permit[5], 1);
    assert.equal(permit.readUInt16LE(6), 172);
    assert.equal(permit.readUInt32LE(152), fixture.scopeMask);
    assert.equal(permit.readUInt32LE(156), 300000);
    assert.equal(
        crypto.verify(
            'sha256',
            permit.subarray(0, 172),
            {
                key: fixture.permitKeys.publicKey,
                dsaEncoding: 'ieee-p1363'
            },
            permit.subarray(172)
        ),
        true
    );
    return permit;
}

test('authentication rate limiter is bounded and returns a retry interval',
    () => {
        let now = 1000;
        const limiter = new SlidingWindowRateLimiter({
            now: () => now,
            windowMs: 1000,
            maxAttempts: 2,
            maxKeys: 2
        });
        limiter.check('client-a');
        limiter.check('client-a');
        assert.throws(
            () => limiter.check('client-a'),
            error => error.code === 'AUTH_RATE_LIMITED' &&
                error.status === 429 &&
                error.retryAfterSeconds === 1
        );
        limiter.check('client-b');
        assert.throws(
            () => limiter.check('client-c'),
            error => error.code === 'AUTH_RATE_LIMIT_CAPACITY' &&
                error.status === 503
        );
        now = 2001;
        limiter.check('client-a');
    });

test('production auth rejects local private keys and requires shared adapters',
    () => {
        const fixture = createFixture();
        try {
            const caPem = fixture.caKeys.publicKey.export({
                type: 'spki',
                format: 'pem'
            });
            const localPermitPem =
                fixture.permitKeys.privateKey.export({
                    type: 'pkcs8',
                    format: 'pem'
                });
            const failClosed = createDeviceAuthV2FromEnvironment(
                fixture.storage,
                {
                    environment: {
                        NODE_ENV: 'production',
                        DEVICE_CA_PUBLIC_KEY_PEM: caPem,
                        WEB_CONFIG_AUTH_PRIVATE_KEY_PEM: localPermitPem
                    }
                }
            );
            assert.equal(failClosed.isReady(), false);

            const shared = createDeviceAuthV2FromEnvironment(
                fixture.storage,
                {
                    environment: {
                        NODE_ENV: 'production',
                        DEVICE_CA_PUBLIC_KEY_PEM: caPem
                    },
                    adapterDependencies: {
                        permitSigner: new BinaryP256PermitSigner(
                            fixture.permitKeys.privateKey
                        ),
                        challengeStore: new MemoryChallengeStore(),
                        tokenStore: new OpaqueTokenStore(),
                        devicePolicy:
                            new StorageDevicePolicy(fixture.storage),
                        challengeLimiter:
                            new SlidingWindowRateLimiter(),
                        verifyLimiter:
                            new SlidingWindowRateLimiter()
                    }
                }
            );
            assert.equal(shared.isReady(), true);
        } finally {
            fs.removeSync(fixture.tempDir);
        }
    });

test('production adapters require a bounded explicit token TTL', () => {
    const makeDependencies = tokenStore => ({
        permitSigner: { sign() {} },
        challengeStore: { issue() {}, consume() {} },
        tokenStore,
        devicePolicy: { check() {}, get() {} },
        challengeLimiter: { check() {} },
        verifyLimiter: { check() {} }
    });
    const tokenMethods = {
        issue() {},
        resolve() {},
        revoke() {}
    };

    for (const ttlMs of [undefined, 0, 300001, 1.5, Number.MAX_VALUE]) {
        const tokenStore = { ...tokenMethods };
        if (ttlMs !== undefined) {
            tokenStore.ttlMs = ttlMs;
        }
        assert.throws(
            () => validateProductionAdapter(
                makeDependencies(tokenStore)
            ),
            /tokenStore\.ttlMs must be a safe integer between 1 and 300000/
        );
    }

    for (const ttlMs of [1, 300000]) {
        assert.equal(
            validateProductionAdapter(
                makeDependencies({ ...tokenMethods, ttlMs })
            ).tokenStore.ttlMs,
            ttlMs
        );
    }
});

test('V2 authenticates fixed binary proofs and signs a scoped permit', async () => {
    const fixture = createFixture();
    try {
        const challenge = issueChallenge(fixture);
        assert.equal(decodeBase64Url(challenge.challengeId, 'id').length, 16);
        assert.equal(Buffer.from(challenge.nonce, 'base64').length, 32);
        assert.ok(challenge.expiresAt > fixture.now());
        const result = await verifySession(
            fixture,
            makeVerifyRequest(fixture, challenge)
        );
        assert.deepEqual(result.grantedScopes, fixture.scopes);
        assert.equal(result.expiresIn, 300);
        assert.equal(result.expiresInMs, 300000);
        assert.equal(Buffer.from(result.sessionSalt, 'base64').length, 32);
        const permit = verifyPermit(fixture, result.deviceSessionPermit);
        assert.deepEqual(permit.subarray(24, 40), fixture.webhidSessionId);
        assert.deepEqual(
            Buffer.from(result.sessionSalt, 'base64'),
            crypto.createHash('sha256').update(permit).digest()
        );
        assert.equal(
            permit.subarray(40, 56).toString('hex'),
            fixture.deviceId.toString('hex')
        );
        assert.equal(
            fixture.tokenStore.resolve(result.apiToken).deviceId,
            fixture.deviceId.toString('hex').toUpperCase()
        );
        assert.equal(
            fixture.tokenStore.tokens.has(result.apiToken),
            false,
            'the store must not retain a usable bearer token as its key'
        );
    } finally {
        fs.removeSync(fixture.tempDir);
    }
});

test('challenge is one-time and invalid proof burns it atomically', async () => {
    const fixture = createFixture();
    try {
        const challenge = issueChallenge(fixture);
        const request = makeVerifyRequest(fixture, challenge);
        const damaged = Buffer.from(
            request.deviceAttestation.signature,
            'base64'
        );
        damaged[353] ^= 0x01;
        request.deviceAttestation.signature = damaged.toString('base64');
        await assert.rejects(
            verifySession(fixture, request),
            error => error.code === 'INVALID_SESSION_PROOF'
        );
        await assert.rejects(
            verifySession(
                fixture,
                makeVerifyRequest(fixture, challenge)
            ),
            error => error.code === 'CHALLENGE_UNKNOWN_OR_USED'
        );
    } finally {
        fs.removeSync(fixture.tempDir);
    }
});

test('expired challenge, revocation and firmware policy fail closed', async () => {
    const expired = createFixture();
    try {
        const challenge = issueChallenge(expired);
        const request = makeVerifyRequest(expired, challenge);
        expired.clock.value += 61000;
        await assert.rejects(
            verifySession(expired, request),
            error => error.code === 'CHALLENGE_EXPIRED'
        );
    } finally {
        fs.removeSync(expired.tempDir);
    }

    const revoked = createFixture();
    try {
        const challenge = issueChallenge(revoked);
        revoked.storage.revokeV2Device(
            revoked.deviceId.toString('hex'),
            'test revocation',
            'test'
        );
        await assert.rejects(
            verifySession(
                revoked,
                makeVerifyRequest(revoked, challenge)
            ),
            error => error.code === 'DEVICE_REVOKED'
        );
    } finally {
        fs.removeSync(revoked.tempDir);
    }

    const rollback = createFixture();
    try {
        const challenge = issueChallenge(rollback);
        rollback.storage.updateV2DevicePolicy(
            rollback.deviceId.toString('hex'),
            {
                minSecurityVersion: rollback.securityVersion + 1,
                allowedFirmwareMeasurements: [
                    rollback.firmwareHash.toString('hex')
                ]
            }
        );
        await assert.rejects(
            verifySession(
                rollback,
                makeVerifyRequest(rollback, challenge)
            ),
            error => error.code === 'FIRMWARE_ROLLBACK_BLOCKED'
        );
    } finally {
        fs.removeSync(rollback.tempDir);
    }
});

function httpRequest(server, options) {
    const address = server.address();
    return new Promise((resolve, reject) => {
        const request = http.request({
            host: '127.0.0.1',
            port: address.port,
            path: options.path,
            method: options.method || 'GET',
            headers: options.headers || {}
        }, response => {
            const chunks = [];
            response.on('data', chunk => chunks.push(chunk));
            response.on('end', () => resolve({
                status: response.statusCode,
                headers: response.headers,
                body: Buffer.concat(chunks)
            }));
        });
        request.on('error', reject);
        if (options.body) {
            request.write(options.body);
        }
        request.end();
    });
}

test('HTTP routes enforce exact CORS, security headers and protected downloads',
    async () => {
        const fixture = createFixture();
        const app = express();
        app.disable('x-powered-by');
        app.use(securityHeaders);
        app.use(cors(createExactCorsOptions([
            'https://firmware.st-dash.com'
        ])));
        app.use(express.json({ limit: '64kb' }));
        app.locals.deviceAuthV2 = fixture.service;
        app.locals.storage_manager = fixture.storage;
        initDeviceAuthV2Routes(
            app,
            fixture.service,
            fixture.storage,
            () => (req, res) => res.status(401).end()
        );
        const firmwareBytes = Buffer.from('signed firmware fixture');
        fs.writeFileSync(
            path.join(fixture.storage.uploadDir, 'firmware.zip'),
            firmwareBytes
        );
        app.use(
            '/downloads',
            fixture.service.requireSession(['firmware.update']),
            express.static(fixture.storage.uploadDir)
        );
        fixture.storage.addFirmware({
            name: 'V2 test firmware',
            version: '2.0.0',
            hardwareVersion: '2.0.0',
            desc: 'integration fixture'
        });
        initAllRoutes(
            app,
            fixture.storage,
            {
                uploadDir: fixture.storage.uploadDir,
                maxFileSize: 1024,
                allowedExtensions: ['.zip'],
                serverUrl: 'https://firmware.st-dash.com'
            },
            () => (req, res) => res.status(401).json({
                error: 'LEGACY_AUTH_REJECTED'
            }),
            () => (req, res) => res.status(401).end(),
            { config: { admin: {} } }
        );
        app.use((error, req, res, next) => {
            res.status(error.status || 500).json({
                error: error.code || 'SERVER_ERROR'
            });
        });
        const server = app.listen(0);
        try {
            const origin = 'https://firmware.st-dash.com';
            const deniedAnonymousCatalog = await httpRequest(server, {
                path: '/api/firmwares'
            });
            assert.equal(deniedAnonymousCatalog.status, 401);

            const readOnlyScopes = ['config.read'];
            const readChallengeResponse = await httpRequest(server, {
                path: '/api/v2/device-auth/challenges',
                method: 'POST',
                headers: {
                    Origin: origin,
                    'Content-Type': 'application/json'
                },
                body: JSON.stringify({
                    protocol: 'hbox-webhid-v1',
                    requestedScopes: readOnlyScopes
                })
            });
            assert.equal(readChallengeResponse.status, 201);
            const readChallenge = JSON.parse(
                readChallengeResponse.body.toString('utf8')
            );
            const readVerifyResponse = await httpRequest(server, {
                path: '/api/v2/device-auth/verify',
                method: 'POST',
                headers: {
                    Origin: origin,
                    'Content-Type': 'application/json'
                },
                body: JSON.stringify(
                    makeVerifyRequest(
                        fixture,
                        readChallenge,
                        readOnlyScopes
                    )
                )
            });
            assert.equal(readVerifyResponse.status, 200);
            const readSession = JSON.parse(
                readVerifyResponse.body.toString('utf8')
            );
            assert.deepEqual(readSession.grantedScopes, readOnlyScopes);

            const readCatalog = await httpRequest(server, {
                path: '/api/firmwares',
                headers: {
                    Authorization: `Bearer ${readSession.apiToken}`
                }
            });
            assert.equal(readCatalog.status, 200);
            const catalogBody = JSON.parse(
                readCatalog.body.toString('utf8')
            );
            assert.equal(catalogBody.total, 1);

            const firmwareId =
                fixture.storage.getFirmwares()[0].id;
            const readCatalogEntry = await httpRequest(server, {
                path: `/api/firmwares/${firmwareId}`,
                headers: {
                    Authorization: `Bearer ${readSession.apiToken}`
                }
            });
            assert.equal(readCatalogEntry.status, 200);

            const readOnlyUpdateCheck = await httpRequest(server, {
                path: '/api/firmware-check-update',
                method: 'POST',
                headers: {
                    Authorization: `Bearer ${readSession.apiToken}`,
                    'Content-Type': 'application/json'
                },
                body: JSON.stringify({ currentVersion: '1.0.0' })
            });
            assert.equal(readOnlyUpdateCheck.status, 200);

            const readOnlyDownload = await httpRequest(server, {
                path: '/downloads/firmware.zip',
                headers: {
                    Authorization: `Bearer ${readSession.apiToken}`
                }
            });
            assert.equal(readOnlyDownload.status, 403);

            const readOnlyWrite = await httpRequest(server, {
                path: `/api/firmwares/${firmwareId}`,
                method: 'PUT',
                headers: {
                    Authorization: `Bearer ${readSession.apiToken}`,
                    'Content-Type': 'application/json'
                },
                body: JSON.stringify({ desc: 'must remain denied' })
            });
            assert.equal(readOnlyWrite.status, 401);

            const challengeResponse = await httpRequest(server, {
                path: '/api/v2/device-auth/challenges',
                method: 'POST',
                headers: {
                    Origin: origin,
                    'Content-Type': 'application/json'
                },
                body: JSON.stringify({
                    protocol: 'hbox-webhid-v1',
                    requestedScopes: fixture.scopes
                })
            });
            assert.equal(challengeResponse.status, 201);
            assert.equal(
                challengeResponse.headers['access-control-allow-origin'],
                origin
            );
            assert.equal(
                challengeResponse.headers['cache-control'],
                'no-store'
            );
            assert.match(
                challengeResponse.headers['permissions-policy'],
                /hid=\(self\)/
            );
            const challenge = JSON.parse(
                challengeResponse.body.toString('utf8')
            );
            const verifyResponse = await httpRequest(server, {
                path: '/api/v2/device-auth/verify',
                method: 'POST',
                headers: {
                    Origin: origin,
                    'Content-Type': 'application/json'
                },
                body: JSON.stringify(
                    makeVerifyRequest(fixture, challenge)
                )
            });
            assert.equal(verifyResponse.status, 200);
            const session = JSON.parse(
                verifyResponse.body.toString('utf8')
            );

            const updateCheck = await httpRequest(server, {
                path: '/api/firmware-check-update',
                method: 'POST',
                headers: {
                    Authorization: `Bearer ${session.apiToken}`,
                    'Content-Type': 'application/json'
                },
                body: JSON.stringify({ currentVersion: '1.0.0' })
            });
            assert.equal(updateCheck.status, 200);
            assert.equal(
                JSON.parse(updateCheck.body.toString('utf8'))
                    .data.hardwareVersion,
                '2.0.0'
            );

            const deniedDownload = await httpRequest(server, {
                path: '/downloads/firmware.zip'
            });
            assert.equal(deniedDownload.status, 401);

            const download = await httpRequest(server, {
                path: '/downloads/firmware.zip',
                headers: {
                    Authorization: `Bearer ${session.apiToken}`
                }
            });
            assert.equal(download.status, 200);
            assert.deepEqual(download.body, firmwareBytes);

            fixture.storage.revokeV2Device(
                fixture.deviceId.toString('hex'),
                'integration test',
                'test'
            );
            const revokedDownload = await httpRequest(server, {
                path: '/downloads/firmware.zip',
                headers: {
                    Authorization: `Bearer ${session.apiToken}`
                }
            });
            assert.equal(revokedDownload.status, 403);

            const untrusted = await httpRequest(server, {
                path: '/api/v2/device-auth/challenges',
                method: 'POST',
                headers: {
                    Origin: 'https://evil.example',
                    'Content-Type': 'application/json'
                },
                body: '{}'
            });
            assert.equal(untrusted.status, 403);
        } finally {
            await new Promise(resolve => server.close(resolve));
            fs.removeSync(fixture.tempDir);
        }
    });

test('firmware metadata PUT remains behind administrator authentication',
    async () => {
        const fixture = createFixture();
        const app = express();
        app.use(express.json());
        const rejectAdmin = () => (req, res) => res.status(401).json({
            error: 'ADMIN_REQUIRED'
        });
        initAllRoutes(
            app,
            fixture.storage,
            {
                uploadDir: fixture.storage.uploadDir,
                maxFileSize: 1024,
                allowedExtensions: ['.zip'],
                serverUrl: 'https://firmware.st-dash.com'
            },
            () => (req, res, next) => next(),
            rejectAdmin,
            { config: { admin: {} } }
        );
        const server = app.listen(0);
        try {
            const response = await httpRequest(server, {
                path: '/api/firmwares/not-found',
                method: 'PUT',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ desc: 'must not update' })
            });
            assert.equal(response.status, 401);
        } finally {
            await new Promise(resolve => server.close(resolve));
            fs.removeSync(fixture.tempDir);
        }
    });

test('legacy V1 catalog compatibility requires an explicit weak-auth header',
    async () => {
        const fixture = createFixture();
        const rawUniqueId = '12345678-9ABCDEF0-13572468';
        const parts = rawUniqueId.split('-').map(value =>
            Number.parseInt(value, 16)
        );
        const deviceId = fixture.storage.calculateDeviceIdHash(...parts);
        const registration = fixture.storage.addDevice({
            deviceId,
            rawUniqueId,
            deviceName: 'Legacy catalog fixture'
        });
        assert.equal(registration.success, true);
        fixture.storage.addFirmware({
            name: 'Legacy catalog firmware',
            version: '1.0.0',
            hardwareVersion: '1.0.0',
            desc: 'legacy compatibility fixture'
        });

        const app = express();
        app.use(express.json());
        app.locals.storage_manager = fixture.storage;
        app.locals.deviceAuthV2 = fixture.service;
        initAllRoutes(
            app,
            fixture.storage,
            {
                uploadDir: fixture.storage.uploadDir,
                maxFileSize: 1024,
                allowedExtensions: ['.zip'],
                serverUrl: 'https://firmware.st-dash.com'
            },
            validateDeviceAuth,
            () => (req, res) => res.status(401).end(),
            { config: { admin: {} } }
        );
        const timestamp = Date.now();
        const challenge = `legacy-${timestamp}`;
        const legacyAuth = Buffer.from(JSON.stringify({
            deviceId,
            challenge,
            timestamp,
            signature: generateDeviceSignature(
                deviceId,
                challenge,
                timestamp
            )
        })).toString('base64');
        const server = app.listen(0);
        try {
            const anonymous = await httpRequest(server, {
                path: '/api/firmwares'
            });
            assert.equal(anonymous.status, 401);

            const catalog = await httpRequest(server, {
                path: '/api/firmwares',
                headers: { 'X-Device-Auth': legacyAuth }
            });
            assert.equal(catalog.status, 200);
            const firmwareId = fixture.storage.getFirmwares()[0].id;
            const details = await httpRequest(server, {
                path: `/api/firmwares/${firmwareId}`,
                headers: { 'X-Device-Auth': legacyAuth }
            });
            assert.equal(details.status, 200);
        } finally {
            await new Promise(resolve => server.close(resolve));
            fs.removeSync(fixture.tempDir);
        }
    });

test('legacy firmware compatibility uses an expiring path ticket', async () => {
    const fixture = createFixture();
    const ticketStore = new LegacyDownloadTicketStore({
        now: fixture.now,
        ttlMs: 1000
    });
    const bytes = Buffer.from('legacy ticket fixture');
    fs.writeFileSync(
        path.join(fixture.storage.uploadDir, 'legacy.zip'),
        bytes
    );
    const device = fixture.storage.findDevice(
        fixture.deviceId.toString('hex')
    );
    const url = createLegacyDownloadUrl(
        ticketStore,
        'https://firmware.st-dash.com',
        { filename: 'legacy.zip' },
        device
    );
    assert.match(url, /\/legacy-downloads\/[A-Za-z0-9_-]{43}\/legacy\.zip$/);

    const app = express();
    initLegacyDownloadRoute(
        app,
        ticketStore,
        fixture.storage,
        fixture.storage.uploadDir
    );
    const server = app.listen(0);
    try {
        const pathname = new URL(url).pathname;
        const accepted = await httpRequest(server, { path: pathname });
        assert.equal(accepted.status, 200);
        assert.deepEqual(accepted.body, bytes);

        fixture.clock.value += 1001;
        const expired = await httpRequest(server, { path: pathname });
        assert.equal(expired.status, 401);
    } finally {
        await new Promise(resolve => server.close(resolve));
        fs.removeSync(fixture.tempDir);
    }
});
