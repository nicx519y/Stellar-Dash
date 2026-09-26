#!/usr/bin/env node
'use strict';

/*
 * Generate the public, deterministic WebHID V1 interoperability fixture.
 *
 * No PEM or private scalar is written to the repository.  Test-only scalar
 * values are derived at runtime from the public labels below.  Consequently
 * these keys are intentionally public and MUST NEVER be used for production,
 * manufacturing, firmware signing, or server authorization.
 */

const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const P256_ORDER = BigInt(
    '0xffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632551'
);
const OUTPUT_PATH = path.resolve(
    __dirname,
    '..',
    'common',
    'test_vectors',
    'webhid_security_v1.json'
);
const LABELS = Object.freeze({
    permit: 'HBox TEST ONLY WebHID V1 permit signing scalar',
    browser: 'HBox TEST ONLY WebHID V1 browser ECDH scalar',
    device: 'HBox TEST ONLY WebHID V1 device ECDH scalar'
});

function sha256(value) {
    return crypto.createHash('sha256').update(value).digest();
}

function hmacSha256(key, ...values) {
    const hmac = crypto.createHmac('sha256', key);
    values.forEach(value => hmac.update(value));
    return hmac.digest();
}

function bytesToBigInt(value) {
    return BigInt(`0x${Buffer.from(value).toString('hex')}`);
}

function bigIntToBytes(value, length = 32) {
    return Buffer.from(value.toString(16).padStart(length * 2, '0'), 'hex');
}

function modulo(value, modulus) {
    const result = value % modulus;
    return result >= 0n ? result : result + modulus;
}

function inverse(value, modulus) {
    let oldR = modulo(value, modulus);
    let r = modulus;
    let oldS = 1n;
    let s = 0n;
    while (r !== 0n) {
        const quotient = oldR / r;
        [oldR, r] = [r, oldR - quotient * r];
        [oldS, s] = [s, oldS - quotient * s];
    }
    if (oldR !== 1n) {
        throw new Error('P-256 scalar has no modular inverse');
    }
    return modulo(oldS, modulus);
}

function deriveTestScalar(label) {
    return bigIntToBytes(
        1n + (bytesToBigInt(sha256(Buffer.from(label, 'utf8'))) %
            (P256_ORDER - 1n))
    );
}

function publicKeyFromScalar(scalar) {
    const ecdh = crypto.createECDH('prime256v1');
    ecdh.setPrivateKey(scalar);
    return ecdh.getPublicKey(undefined, 'uncompressed');
}

/*
 * RFC 6979 deterministic ECDSA over P-256.  Node's ECDSA signing API uses a
 * randomized nonce, which would make a checked-in golden fixture drift.
 */
function deterministicP256Sign(scalarBytes, message) {
    const digest = sha256(message);
    const x = Buffer.from(scalarBytes);
    const h1 = bigIntToBytes(bytesToBigInt(digest) % P256_ORDER);
    let kState = Buffer.alloc(32, 0x00);
    let vState = Buffer.alloc(32, 0x01);

    kState = hmacSha256(kState, vState, Buffer.from([0x00]), x, h1);
    vState = hmacSha256(kState, vState);
    kState = hmacSha256(kState, vState, Buffer.from([0x01]), x, h1);
    vState = hmacSha256(kState, vState);

    let nonce;
    while (true) {
        vState = hmacSha256(kState, vState);
        nonce = bytesToBigInt(vState);
        if (nonce > 0n && nonce < P256_ORDER) {
            break;
        }
        kState = hmacSha256(
            kState,
            vState,
            Buffer.from([0x00])
        );
        vState = hmacSha256(kState, vState);
    }

    const nonceEcdh = crypto.createECDH('prime256v1');
    nonceEcdh.setPrivateKey(bigIntToBytes(nonce));
    const noncePoint = nonceEcdh.getPublicKey(undefined, 'uncompressed');
    const r = bytesToBigInt(noncePoint.subarray(1, 33)) % P256_ORDER;
    const privateValue = bytesToBigInt(scalarBytes);
    const z = bytesToBigInt(digest);
    let s = modulo(
        inverse(nonce, P256_ORDER) * (z + r * privateValue),
        P256_ORDER
    );
    if (r === 0n || s === 0n) {
        throw new Error('RFC 6979 produced an invalid P-256 signature');
    }
    // A canonical low-S signature removes the remaining representation choice.
    if (s > P256_ORDER / 2n) {
        s = P256_ORDER - s;
    }
    return Buffer.concat([bigIntToBytes(r), bigIntToBytes(s)]);
}

function rawPublicKeyToKeyObject(raw) {
    return crypto.createPublicKey({
        key: {
            kty: 'EC',
            crv: 'P-256',
            x: raw.subarray(1, 33).toString('base64url'),
            y: raw.subarray(33, 65).toString('base64url')
        },
        format: 'jwk'
    });
}

function makePermit(permitPublic, browserPublic, devicePublic) {
    const permit = Buffer.alloc(236);
    permit.writeUInt32LE(0x31505348, 0); // "HSP1"
    permit[4] = 1;
    permit[5] = 1;
    permit.writeUInt16LE(172, 6);
    Buffer.from('00112233445566778899aabbccddeeff', 'hex').copy(permit, 8);
    Buffer.from('102132435465768798a9bacbdcedfe0f', 'hex').copy(permit, 24);
    Buffer.from('67de7d274bfd88d86f78f88b1536d065', 'hex').copy(permit, 40);
    Buffer.from(
        '000102030405060708090a0b0c0d0e0f' +
        '101112131415161718191a1b1c1d1e1f',
        'hex'
    ).copy(permit, 56);
    sha256(browserPublic).copy(permit, 88);
    sha256(devicePublic).copy(permit, 120);
    permit.writeUInt32LE(0x00000007, 152);
    // Connection-bound WebHID sessions reserve all lifetime fields as zero.
    permit.writeUInt32LE(0, 156);
    permit.writeUInt32LE(0, 160);
    permit.writeUInt32LE(0, 164);
    permit.writeUInt32LE(1, 168);

    const signature = deterministicP256Sign(
        deriveTestScalar(LABELS.permit),
        permit.subarray(0, 172)
    );
    signature.copy(permit, 172);
    if (!crypto.verify(
        'sha256',
        permit.subarray(0, 172),
        {
            key: rawPublicKeyToKeyObject(permitPublic),
            dsaEncoding: 'ieee-p1363'
        },
        signature
    )) {
        throw new Error('generated permit signature did not verify');
    }
    return permit;
}

function hkdf(sharedSecret, salt, sessionId, direction, nonce = false) {
    const context = Buffer.from(`HBox WebHID v1\0${sessionId}`, 'utf8');
    const suffix = Buffer.from(
        nonce ? `\0${direction}\0nonce` : `\0${direction}`,
        'utf8'
    );
    return Buffer.from(
        crypto.hkdfSync(
            'sha256',
            sharedSecret,
            salt,
            Buffer.concat([context, suffix]),
            nonce ? 8 : 32
        )
    );
}

function makeNonce(prefix, sequence) {
    const nonce = Buffer.alloc(12);
    prefix.copy(nonce, 0);
    nonce.writeUInt32BE(sequence, 8);
    return nonce;
}

function makeReport(key, noncePrefix, type, flags, sequence, plaintext) {
    const report = Buffer.alloc(64);
    report[0] = 1;
    report[1] = type;
    report[2] = flags | 0x01;
    report[3] = plaintext.length;
    report.writeUInt32LE(sequence, 4);
    const cipher = crypto.createCipheriv(
        'aes-256-gcm',
        key,
        makeNonce(noncePrefix, sequence),
        { authTagLength: 12 }
    );
    cipher.setAAD(report.subarray(0, 8), {
        plaintextLength: plaintext.length
    });
    const ciphertext = Buffer.concat([
        cipher.update(plaintext),
        cipher.final()
    ]);
    ciphertext.copy(report, 8);
    cipher.getAuthTag().copy(report, 52);
    return report;
}

function generateVector() {
    const permitScalar = deriveTestScalar(LABELS.permit);
    const browserScalar = deriveTestScalar(LABELS.browser);
    const deviceScalar = deriveTestScalar(LABELS.device);
    const permitPublic = publicKeyFromScalar(permitScalar);
    const browserPublic = publicKeyFromScalar(browserScalar);
    const devicePublic = publicKeyFromScalar(deviceScalar);
    const browserEcdh = crypto.createECDH('prime256v1');
    browserEcdh.setPrivateKey(browserScalar);
    const sharedSecret = browserEcdh.computeSecret(devicePublic);
    const permit = makePermit(permitPublic, browserPublic, devicePublic);
    const permitHash = sha256(permit);
    const sessionId = permit.subarray(24, 40).toString('base64url');
    const browserToDeviceKey = hkdf(
        sharedSecret,
        permitHash,
        sessionId,
        'browser-to-device'
    );
    const deviceToBrowserKey = hkdf(
        sharedSecret,
        permitHash,
        sessionId,
        'device-to-browser'
    );
    const browserToDeviceNoncePrefix = hkdf(
        sharedSecret,
        permitHash,
        sessionId,
        'browser-to-device',
        true
    );
    const deviceToBrowserNoncePrefix = hkdf(
        sharedSecret,
        permitHash,
        sessionId,
        'device-to-browser',
        true
    );
    const browserSequence = 0x01020304;
    const deviceSequence = 0x0a0b0c0d;
    const browserPlaintext = Buffer.from(
        '{"command":"get_global_config"}',
        'utf8'
    );
    const devicePlaintext = Buffer.from(
        '{"transactionId":7,"errNo":0}',
        'utf8'
    );
    const browserReport = makeReport(
        browserToDeviceKey,
        browserToDeviceNoncePrefix,
        0x10,
        0x0c,
        browserSequence,
        browserPlaintext
    );
    const deviceReport = makeReport(
        deviceToBrowserKey,
        deviceToBrowserNoncePrefix,
        0x11,
        0x04,
        deviceSequence,
        devicePlaintext
    );

    return {
        schema: 'hbox-webhid-security-golden-v1',
        protocolVersion: 1,
        warning:
            'Public deterministic TEST ONLY values; never use these keys in production.',
        derivationLabels: LABELS,
        permit: {
            size: 236,
            signedSize: 172,
            authorizationPublicKeySec1Hex: permitPublic.toString('hex'),
            signedBytesHex: permit.subarray(0, 172).toString('hex'),
            signatureP1363Hex: permit.subarray(172).toString('hex'),
            bytesHex: permit.toString('hex'),
            sha256Hex: permitHash.toString('hex')
        },
        session: {
            sessionId,
            browserPublicKeySec1Hex: browserPublic.toString('hex'),
            devicePublicKeySec1Hex: devicePublic.toString('hex'),
            sharedSecretHex: sharedSecret.toString('hex'),
            saltHex: permitHash.toString('hex'),
            browserToDeviceKeyHex: browserToDeviceKey.toString('hex'),
            deviceToBrowserKeyHex: deviceToBrowserKey.toString('hex'),
            browserToDeviceNoncePrefixHex:
                browserToDeviceNoncePrefix.toString('hex'),
            deviceToBrowserNoncePrefixHex:
                deviceToBrowserNoncePrefix.toString('hex'),
            browserSequence,
            deviceSequence,
            browserToDeviceNonceHex: makeNonce(
                browserToDeviceNoncePrefix,
                browserSequence
            ).toString('hex'),
            deviceToBrowserNonceHex: makeNonce(
                deviceToBrowserNoncePrefix,
                deviceSequence
            ).toString('hex')
        },
        reports: {
            browserToDevice: {
                type: 16,
                flags: 13,
                sequence: browserSequence,
                plaintextHex: browserPlaintext.toString('hex'),
                reportHex: browserReport.toString('hex')
            },
            deviceToBrowser: {
                type: 17,
                flags: 5,
                sequence: deviceSequence,
                plaintextHex: devicePlaintext.toString('hex'),
                reportHex: deviceReport.toString('hex')
            }
        }
    };
}

const serialized = `${JSON.stringify(generateVector(), null, 2)}\n`;
if (process.argv.includes('--check')) {
    const current = fs.readFileSync(OUTPUT_PATH, 'utf8');
    if (current !== serialized) {
        console.error(
            'WebHID security golden vector is stale; run with --write.'
        );
        process.exitCode = 1;
    }
} else if (process.argv.includes('--write')) {
    fs.mkdirSync(path.dirname(OUTPUT_PATH), { recursive: true });
    fs.writeFileSync(OUTPUT_PATH, serialized, 'utf8');
    console.log(`wrote ${OUTPUT_PATH}`);
} else {
    process.stdout.write(serialized);
}
