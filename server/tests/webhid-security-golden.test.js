'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const path = require('node:path');
const test = require('node:test');

const vector = require(path.resolve(
    __dirname,
    '..',
    '..',
    'common',
    'test_vectors',
    'webhid_security_v1.json'
));
const {
    normalizeP256PublicKey
} = require('../src/device-auth-v2');

const P256_ORDER = BigInt(
    '0xffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632551'
);

function fromHex(value, label) {
    assert.equal(typeof value, 'string', `${label} must be hexadecimal`);
    assert.match(value, /^(?:[0-9a-f]{2})+$/);
    return Buffer.from(value, 'hex');
}

function deriveTestScalar(label) {
    const digest = crypto.createHash('sha256')
        .update(label, 'utf8')
        .digest();
    const scalar = 1n +
        (BigInt(`0x${digest.toString('hex')}`) % (P256_ORDER - 1n));
    return Buffer.from(scalar.toString(16).padStart(64, '0'), 'hex');
}

function publicKeyFromScalar(scalar) {
    const ecdh = crypto.createECDH('prime256v1');
    ecdh.setPrivateKey(scalar);
    return ecdh.getPublicKey(undefined, 'uncompressed');
}

function derive(sharedSecret, salt, sessionId, direction, length) {
    const context = Buffer.from(`HBox WebHID v1\0${sessionId}`, 'utf8');
    const nonceSuffix = length === 8 ? '\0nonce' : '';
    const info = Buffer.concat([
        context,
        Buffer.from(`\0${direction}${nonceSuffix}`, 'utf8')
    ]);
    return Buffer.from(
        crypto.hkdfSync(
            'sha256',
            sharedSecret,
            salt,
            info,
            length
        )
    );
}

function decryptReport(report, key, noncePrefix) {
    assert.equal(report.length, 64);
    const payloadLength = report[3];
    const sequence = report.readUInt32LE(4);
    const nonce = Buffer.alloc(12);
    noncePrefix.copy(nonce);
    nonce.writeUInt32BE(sequence, 8);
    const decipher = crypto.createDecipheriv(
        'aes-256-gcm',
        key,
        nonce,
        { authTagLength: 12 }
    );
    decipher.setAAD(report.subarray(0, 8), { plaintextLength: payloadLength });
    decipher.setAuthTag(report.subarray(52, 64));
    return Buffer.concat([
        decipher.update(report.subarray(8, 8 + payloadLength)),
        decipher.final()
    ]);
}

test('golden permit bytes match the packed C ABI and verify as P1363 P-256',
    () => {
        const permit = fromHex(vector.permit.bytesHex, 'permit');
        const signed = fromHex(
            vector.permit.signedBytesHex,
            'permit signed bytes'
        );
        const signature = fromHex(
            vector.permit.signatureP1363Hex,
            'permit signature'
        );
        const publicKeyRaw = fromHex(
            vector.permit.authorizationPublicKeySec1Hex,
            'authorization public key'
        );

        assert.equal(vector.schema, 'hbox-webhid-security-golden-v1');
        assert.equal(permit.length, 236);
        assert.equal(permit.length, vector.permit.size);
        assert.equal(signed.length, 172);
        assert.equal(signed.length, vector.permit.signedSize);
        assert.equal(signature.length, 64);
        assert.deepEqual(permit.subarray(0, 172), signed);
        assert.deepEqual(permit.subarray(172), signature);
        assert.equal(permit.readUInt32LE(0), 0x31505348);
        assert.equal(permit[4], 1);
        assert.equal(permit[5], 1);
        assert.equal(permit.readUInt16LE(6), 172);
        assert.equal(permit.readUInt32LE(152), 0x07);
        assert.equal(permit.readUInt32LE(156), 0);
        assert.equal(permit.readUInt32LE(160), 0);
        assert.equal(permit.readUInt32LE(164), 0);
        assert.equal(permit.readUInt32LE(168), 1);

        const publicKey = normalizeP256PublicKey(
            publicKeyRaw,
            'authorizationPublicKey'
        ).key;
        assert.equal(
            crypto.verify(
                'sha256',
                signed,
                { key: publicKey, dsaEncoding: 'ieee-p1363' },
                signature
            ),
            true
        );
        assert.equal(
            crypto.createHash('sha256').update(permit).digest('hex'),
            vector.permit.sha256Hex
        );

        const damaged = Buffer.from(signed);
        damaged[40] ^= 0x01;
        assert.equal(
            crypto.verify(
                'sha256',
                damaged,
                { key: publicKey, dsaEncoding: 'ieee-p1363' },
                signature
            ),
            false
        );
    });

test('golden P-256 ECDH and both HKDF directions are byte exact', () => {
    const browserScalar = deriveTestScalar(
        vector.derivationLabels.browser
    );
    const deviceScalar = deriveTestScalar(
        vector.derivationLabels.device
    );
    const browserPublic = publicKeyFromScalar(browserScalar);
    const devicePublic = publicKeyFromScalar(deviceScalar);
    assert.equal(
        browserPublic.toString('hex'),
        vector.session.browserPublicKeySec1Hex
    );
    assert.equal(
        devicePublic.toString('hex'),
        vector.session.devicePublicKeySec1Hex
    );

    const browserEcdh = crypto.createECDH('prime256v1');
    browserEcdh.setPrivateKey(browserScalar);
    const deviceEcdh = crypto.createECDH('prime256v1');
    deviceEcdh.setPrivateKey(deviceScalar);
    const browserShared = browserEcdh.computeSecret(devicePublic);
    const deviceShared = deviceEcdh.computeSecret(browserPublic);
    assert.deepEqual(browserShared, deviceShared);
    assert.equal(
        browserShared.toString('hex'),
        vector.session.sharedSecretHex
    );

    const salt = fromHex(vector.session.saltHex, 'HKDF salt');
    const directions = [
        [
            'browser-to-device',
            'browserToDeviceKeyHex',
            'browserToDeviceNoncePrefixHex'
        ],
        [
            'device-to-browser',
            'deviceToBrowserKeyHex',
            'deviceToBrowserNoncePrefixHex'
        ]
    ];
    for (const [direction, keyField, nonceField] of directions) {
        assert.equal(
            derive(
                browserShared,
                salt,
                vector.session.sessionId,
                direction,
                32
            ).toString('hex'),
            vector.session[keyField]
        );
        assert.equal(
            derive(
                browserShared,
                salt,
                vector.session.sessionId,
                direction,
                8
            ).toString('hex'),
            vector.session[nonceField]
        );
    }
});

test('golden 64-byte reports decrypt in both directions and reject tampering',
    () => {
        const cases = [
            {
                value: vector.reports.browserToDevice,
                key: vector.session.browserToDeviceKeyHex,
                prefix: vector.session.browserToDeviceNoncePrefixHex
            },
            {
                value: vector.reports.deviceToBrowser,
                key: vector.session.deviceToBrowserKeyHex,
                prefix: vector.session.deviceToBrowserNoncePrefixHex
            }
        ];
        for (const item of cases) {
            const report = fromHex(item.value.reportHex, 'secure HID report');
            const key = fromHex(item.key, 'AES key');
            const prefix = fromHex(item.prefix, 'nonce prefix');
            assert.equal(report.length, 64);
            assert.equal(report[0], 1);
            assert.equal(report[1], item.value.type);
            assert.equal(report[2], item.value.flags);
            assert.equal(report.readUInt32LE(4), item.value.sequence);
            assert.equal(
                decryptReport(report, key, prefix).toString('hex'),
                item.value.plaintextHex
            );

            for (const offset of [4, 8, 63]) {
                const damaged = Buffer.from(report);
                damaged[offset] ^= 0x01;
                assert.throws(
                    () => decryptReport(damaged, key, prefix),
                    /authenticate|Unsupported state/
                );
            }
        }
    });

test('golden fixture stores no PEM or private scalar field', () => {
    const visit = value => {
        if (!value || typeof value !== 'object') {
            return;
        }
        for (const [key, child] of Object.entries(value)) {
            assert.doesNotMatch(
                key,
                /private(?:Key)?(?:Hex|Pem)?|scalarHex|pkcs8/i
            );
            visit(child);
        }
    };
    visit(vector);
});
