import {
  HidSessionCipher,
  SECURE_HID_TAG_SIZE,
} from './secure-hid-frame';
import { DeviceTransportError } from './types';

const GCM_TAG_BITS = SECURE_HID_TAG_SIZE * 8;
const NONCE_PREFIX_SIZE = 8;

export interface BrowserSessionKeys {
  txKey: CryptoKey;
  rxKey: CryptoKey;
  txNoncePrefix: Uint8Array;
  rxNoncePrefix: Uint8Array;
}

export class AesGcmHidSessionCipher implements HidSessionCipher {
  constructor(private readonly keys: BrowserSessionKeys) {
    if (
      keys.txNoncePrefix.byteLength !== NONCE_PREFIX_SIZE ||
      keys.rxNoncePrefix.byteLength !== NONCE_PREFIX_SIZE
    ) {
      throw new DeviceTransportError('protocol', 'HID AES-GCM nonce prefix must be 8 bytes');
    }
  }

  async seal(
    header: Uint8Array,
    sequence: number,
    plaintext: Uint8Array,
  ): Promise<{ ciphertext: Uint8Array; tag: Uint8Array }> {
    const encrypted = new Uint8Array(
      await crypto.subtle.encrypt(
        {
          name: 'AES-GCM',
          iv: makeNonce(this.keys.txNoncePrefix, sequence),
          additionalData: header,
          tagLength: GCM_TAG_BITS,
        },
        this.keys.txKey,
        plaintext,
      ),
    );
    return {
      ciphertext: encrypted.slice(0, encrypted.byteLength - SECURE_HID_TAG_SIZE),
      tag: encrypted.slice(encrypted.byteLength - SECURE_HID_TAG_SIZE),
    };
  }

  async open(
    header: Uint8Array,
    sequence: number,
    ciphertext: Uint8Array,
    tag: Uint8Array,
  ): Promise<Uint8Array> {
    const encrypted = new Uint8Array(ciphertext.byteLength + tag.byteLength);
    encrypted.set(ciphertext);
    encrypted.set(tag, ciphertext.byteLength);
    try {
      return new Uint8Array(
        await crypto.subtle.decrypt(
          {
            name: 'AES-GCM',
            iv: makeNonce(this.keys.rxNoncePrefix, sequence),
            additionalData: header,
            tagLength: GCM_TAG_BITS,
          },
          this.keys.rxKey,
          encrypted,
        ),
      );
    } catch (error) {
      throw new DeviceTransportError('authentication-failed', 'Secure HID report authentication failed', error);
    }
  }
}

export async function deriveBrowserSessionKeys(
  browserPrivateKey: CryptoKey,
  devicePublicKeyRaw: Uint8Array,
  salt: Uint8Array,
  sessionId: string,
): Promise<BrowserSessionKeys> {
  const devicePublicKey = await crypto.subtle.importKey(
    'raw',
    devicePublicKeyRaw,
    { name: 'ECDH', namedCurve: 'P-256' },
    false,
    [],
  );
  const sharedSecret = await crypto.subtle.deriveBits(
    { name: 'ECDH', public: devicePublicKey },
    browserPrivateKey,
    256,
  );
  const hkdfKey = await crypto.subtle.importKey('raw', sharedSecret, 'HKDF', false, ['deriveBits', 'deriveKey']);
  const context = new TextEncoder().encode(`HBox WebHID v1\0${sessionId}`);

  const deriveKey = (direction: string) => crypto.subtle.deriveKey(
    {
      name: 'HKDF',
      hash: 'SHA-256',
      salt,
      info: concatBytes(context, new TextEncoder().encode(`\0${direction}`)),
    },
    hkdfKey,
    { name: 'AES-GCM', length: 256 },
    false,
    ['encrypt', 'decrypt'],
  );
  const derivePrefix = async (direction: string) => new Uint8Array(
    await crypto.subtle.deriveBits(
      {
        name: 'HKDF',
        hash: 'SHA-256',
        salt,
        info: concatBytes(context, new TextEncoder().encode(`\0${direction}\0nonce`)),
      },
      hkdfKey,
      NONCE_PREFIX_SIZE * 8,
    ),
  );

  return {
    txKey: await deriveKey('browser-to-device'),
    rxKey: await deriveKey('device-to-browser'),
    txNoncePrefix: await derivePrefix('browser-to-device'),
    rxNoncePrefix: await derivePrefix('device-to-browser'),
  };
}

function makeNonce(prefix: Uint8Array, sequence: number): Uint8Array {
  const nonce = new Uint8Array(12);
  nonce.set(prefix);
  new DataView(nonce.buffer).setUint32(8, sequence, false);
  return nonce;
}

function concatBytes(first: Uint8Array, second: Uint8Array): Uint8Array {
  const result = new Uint8Array(first.byteLength + second.byteLength);
  result.set(first);
  result.set(second, first.byteLength);
  return result;
}

