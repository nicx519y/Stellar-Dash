const test = require('node:test');
const assert = require('node:assert/strict');
const { webcrypto } = require('node:crypto');

globalThis.crypto ??= webcrypto;

const {
  DirectDeviceSessionClient,
} = require('../lib/device-transport/device-session-client.ts');

test('direct WebHID session opens without certificate or server permit', async () => {
  const devicePair = await crypto.subtle.generateKey(
    { name: 'ECDH', namedCurve: 'P-256' }, true, ['deriveBits'],
  );
  const devicePublic = Buffer.from(
    await crypto.subtle.exportKey('raw', devicePair.publicKey),
  );
  const salt = crypto.getRandomValues(new Uint8Array(32));
  let request;
  let established;
  const transport = {
    session: { productName: 'HBox' },
    setAuthenticating() {},
    async bootstrapRequest(command, params) {
      request = { command, params };
      return {
        sessionId: '0123456789abcdef01234567',
        sessionSalt: Buffer.from(salt).toString('base64'),
        deviceEphemeralPublicKey: devicePublic.toString('base64'),
        hardwareVersion: '2.0.0',
      };
    },
    establishSecureSession(cipher, session) {
      established = { cipher, session };
      this.session = session;
    },
  };

  const client = new DirectDeviceSessionClient();
  const session = await client.authenticate(transport);
  assert.equal(request.command, 'session.open-direct');
  assert.equal(session.webConfigProfile, 'hbox-pcb-v2');
  assert.equal(client.hasScopes(['config.write', 'firmware.update']), true);
  assert.equal(established.session, session);

  const browserPublic = await crypto.subtle.importKey(
    'raw',
    Buffer.from(request.params.browserEphemeralPublicKey, 'base64'),
    { name: 'ECDH', namedCurve: 'P-256' },
    false,
    [],
  );
  const shared = await crypto.subtle.deriveBits(
    { name: 'ECDH', public: browserPublic }, devicePair.privateKey, 256,
  );
  const hkdfKey = await crypto.subtle.importKey(
    'raw', shared, 'HKDF', false, ['deriveKey', 'deriveBits'],
  );
  const browserToDeviceKey = await crypto.subtle.deriveKey(
    {
      name: 'HKDF', hash: 'SHA-256', salt,
      info: new TextEncoder().encode(`HBox WebHID v1\0${session.sessionId}\0browser-to-device`),
    },
    hkdfKey,
    { name: 'AES-GCM', length: 256 },
    false,
    ['decrypt'],
  );
  const header = new Uint8Array([1, 2, 3]);
  const plaintext = new TextEncoder().encode('{"command":"get_global_config"}');
  const sealed = await established.cipher.seal(header, 1, plaintext);
  const nonce = new Uint8Array(12);
  const noncePrefix = await crypto.subtle.deriveBits(
    {
      name: 'HKDF', hash: 'SHA-256', salt,
      info: new TextEncoder().encode(`HBox WebHID v1\0${session.sessionId}\0browser-to-device\0nonce`),
    },
    hkdfKey,
    64,
  );
  assert.deepEqual(new Uint8Array(noncePrefix), established.cipher.keys.txNoncePrefix);
  nonce.set(new Uint8Array(noncePrefix));
  new DataView(nonce.buffer).setUint32(8, 1, false);
  const combined = new Uint8Array(sealed.ciphertext.length + sealed.tag.length);
  combined.set(sealed.ciphertext);
  combined.set(sealed.tag, sealed.ciphertext.length);
  const opened = await crypto.subtle.decrypt(
    { name: 'AES-GCM', iv: nonce, additionalData: header, tagLength: 96 },
    browserToDeviceKey,
    combined,
  );
  assert.deepEqual(new Uint8Array(opened), plaintext);
});

test('malformed direct-session salt cannot establish a WebHID session', async () => {
  const client = new DirectDeviceSessionClient();
  let established = false;
  const transport = {
    setAuthenticating() {},
    async bootstrapRequest() {
      return {
        sessionId: '0123456789abcdef01234567',
        sessionSalt: 'AA==',
        deviceEphemeralPublicKey: 'AA==',
        hardwareVersion: '2.0.0',
      };
    },
    establishSecureSession() { established = true; },
  };
  await assert.rejects(client.authenticate(transport), /参数无效/);
  assert.equal(established, false);
});
