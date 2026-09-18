const test = require('node:test');
const assert = require('node:assert/strict');
const { createHash, webcrypto } = require('node:crypto');
const path = require('node:path');

if (!globalThis.crypto) {
  globalThis.crypto = webcrypto;
}

const vector = require(path.resolve(
  __dirname,
  '..',
  '..',
  '..',
  'common',
  'test_vectors',
  'webhid_security_v1.json',
));
const {
  SecureHidFrameFlags,
  SecureHidFrameType,
  SecureHidReportCodec,
} = require('../lib/device-transport/secure-hid-frame.ts');
const {
  AesGcmHidSessionCipher,
  deriveBrowserSessionKeys,
} = require('../lib/device-transport/session-crypto.ts');

const P256_ORDER = BigInt(
  '0xffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632551',
);

function fromHex(value) {
  assert.match(value, /^(?:[0-9a-f]{2})+$/);
  return Uint8Array.from(Buffer.from(value, 'hex'));
}

function toHex(value) {
  return Buffer.from(value).toString('hex');
}

function toBase64Url(value) {
  return Buffer.from(value).toString('base64url');
}

function deriveTestScalar(label) {
  const digest = createHash('sha256').update(label, 'utf8').digest();
  const scalar = 1n +
    (BigInt(`0x${digest.toString('hex')}`) % (P256_ORDER - 1n));
  return Buffer.from(scalar.toString(16).padStart(64, '0'), 'hex');
}

async function importBrowserTestPrivateKey() {
  const rawPublic = Buffer.from(
    vector.session.browserPublicKeySec1Hex,
    'hex',
  );
  return crypto.subtle.importKey(
    'jwk',
    {
      kty: 'EC',
      crv: 'P-256',
      x: toBase64Url(rawPublic.subarray(1, 33)),
      y: toBase64Url(rawPublic.subarray(33, 65)),
      d: toBase64Url(deriveTestScalar(vector.derivationLabels.browser)),
      ext: true,
      key_ops: ['deriveBits'],
    },
    { name: 'ECDH', namedCurve: 'P-256' },
    false,
    ['deriveBits'],
  );
}

test('browser verifies the fixed raw P-256 permit signature', async () => {
  const rawPublic = fromHex(
    vector.permit.authorizationPublicKeySec1Hex,
  );
  const publicKey = await crypto.subtle.importKey(
    'raw',
    rawPublic,
    { name: 'ECDSA', namedCurve: 'P-256' },
    false,
    ['verify'],
  );
  assert.equal(
    await crypto.subtle.verify(
      { name: 'ECDSA', hash: 'SHA-256' },
      publicKey,
      fromHex(vector.permit.signatureP1363Hex),
      fromHex(vector.permit.signedBytesHex),
    ),
    true,
  );

  const damaged = fromHex(vector.permit.signedBytesHex);
  damaged[40] ^= 0x01;
  assert.equal(
    await crypto.subtle.verify(
      { name: 'ECDSA', hash: 'SHA-256' },
      publicKey,
      fromHex(vector.permit.signatureP1363Hex),
      damaged,
    ),
    false,
  );
});

test('browser ECDH/HKDF derives the byte-exact directional session material',
  async () => {
    const keys = await deriveBrowserSessionKeys(
      await importBrowserTestPrivateKey(),
      fromHex(vector.session.devicePublicKeySec1Hex),
      fromHex(vector.session.saltHex),
      vector.session.sessionId,
    );
    assert.equal(
      toHex(keys.txNoncePrefix),
      vector.session.browserToDeviceNoncePrefixHex,
    );
    assert.equal(
      toHex(keys.rxNoncePrefix),
      vector.session.deviceToBrowserNoncePrefixHex,
    );

    const cipher = new AesGcmHidSessionCipher(keys);
    const codec = new SecureHidReportCodec(cipher);
    const browser = vector.reports.browserToDevice;
    const encoded = await codec.encode({
      type: SecureHidFrameType.RPC_REQUEST,
      flags: SecureHidFrameFlags.LAST |
        SecureHidFrameFlags.ACK_REQUIRED,
      sequence: browser.sequence,
      payload: fromHex(browser.plaintextHex),
      secure: true,
    });
    assert.equal(toHex(encoded), browser.reportHex);

    const device = vector.reports.deviceToBrowser;
    const decoded = await codec.decode(fromHex(device.reportHex));
    assert.equal(decoded.type, SecureHidFrameType.RPC_RESPONSE);
    assert.equal(decoded.flags, device.flags);
    assert.equal(decoded.sequence, device.sequence);
    assert.equal(toHex(decoded.payload), device.plaintextHex);
  });

test('browser rejects header, ciphertext, and tag tampering on golden reports',
  async () => {
    const keys = await deriveBrowserSessionKeys(
      await importBrowserTestPrivateKey(),
      fromHex(vector.session.devicePublicKeySec1Hex),
      fromHex(vector.session.saltHex),
      vector.session.sessionId,
    );
    const codec = new SecureHidReportCodec(
      new AesGcmHidSessionCipher(keys),
    );
    for (const offset of [4, 8, 63]) {
      const damaged = fromHex(vector.reports.deviceToBrowser.reportHex);
      damaged[offset] ^= 0x01;
      await assert.rejects(
        codec.decode(damaged),
        /authentication failed/,
      );
    }
  });

test('golden fixture has no stored PEM or private scalar property', () => {
  const visit = (value) => {
    if (!value || typeof value !== 'object') {
      return;
    }
    for (const [key, child] of Object.entries(value)) {
      assert.doesNotMatch(
        key,
        /private(?:Key)?(?:Hex|Pem)?|scalarHex|pkcs8/i,
      );
      visit(child);
    }
  };
  visit(vector);
});
