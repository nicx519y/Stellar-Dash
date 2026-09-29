import { AesGcmHidSessionCipher, deriveBrowserSessionKeys } from './session-crypto';
import { DeviceScope, DeviceSession, DeviceTransportError } from './types';
import { SUPPORTED_WEB_CONFIG_TARGETS } from './webconfig-target';
import { WebHidTransport } from './webhid-transport';

export interface DeviceSessionClient {
  authenticate(
    transport: WebHidTransport,
    scopes?: readonly DeviceScope[],
    signal?: AbortSignal,
    onAuthorizing?: () => void,
  ): Promise<DeviceSession>;
  reauthorize(
    transport: WebHidTransport,
    scopes: readonly DeviceScope[],
    signal?: AbortSignal,
  ): Promise<DeviceSession>;
  authorizedFetch(
    input: RequestInfo | URL,
    init?: RequestInit,
    requiredScopes?: readonly DeviceScope[],
  ): Promise<Response>;
  hasScopes(scopes: readonly DeviceScope[]): boolean;
  clear(): void;
}

const ALL_SCOPES: readonly DeviceScope[] = [
  'config.read',
  'config.write',
  'monitor.read',
  'device.control',
  'asset.write',
  'firmware.update',
] as const;

interface DirectOpenReply {
  sessionId: string;
  sessionSalt: string;
  deviceEphemeralPublicKey: string;
  hardwareVersion: string;
}

/** Establishes an encrypted WebHID session without device identity or a server permit. */
export class DirectDeviceSessionClient implements DeviceSessionClient {
  private active = false;

  async authenticate(
    transport: WebHidTransport,
    _scopes: readonly DeviceScope[] = ALL_SCOPES,
    signal?: AbortSignal,
    onAuthorizing?: () => void,
  ): Promise<DeviceSession> {
    this.clear();
    if (signal?.aborted) throw new DeviceTransportError('disconnected', '连接已取消');
    transport.setAuthenticating();
    const browserKeyPair = await crypto.subtle.generateKey(
      { name: 'ECDH', namedCurve: 'P-256' },
      true,
      ['deriveBits'],
    );
    const browserPublicKey = new Uint8Array(
      await crypto.subtle.exportKey('raw', browserKeyPair.publicKey),
    );
    onAuthorizing?.();
    const reply = await transport.bootstrapRequest<DirectOpenReply>(
      'session.open-direct',
      { browserEphemeralPublicKey: toBase64(browserPublicKey) },
      { signal, timeoutMs: 10_000 },
    );
    if (signal?.aborted) throw new DeviceTransportError('disconnected', '连接已取消');
    const deviceKey = fromBase64(reply.deviceEphemeralPublicKey);
    const salt = fromBase64(reply.sessionSalt);
    if (!/^[0-9a-f]{24}$/.test(reply.sessionId) ||
        deviceKey.length !== 65 || deviceKey[0] !== 0x04 ||
        salt.length !== 32) {
      throw new DeviceTransportError('protocol', '设备返回的直连会话参数无效');
    }
    const target = SUPPORTED_WEB_CONFIG_TARGETS.find(
      (entry) => entry.pcbRevision === reply.hardwareVersion,
    );
    if (!target) {
      throw new DeviceTransportError('protocol', '此 WebConfig 不支持该 PCB 版本');
    }
    const keys = await deriveBrowserSessionKeys(
      browserKeyPair.privateKey,
      deviceKey,
      salt,
      reply.sessionId,
    );
    if (signal?.aborted) throw new DeviceTransportError('disconnected', '连接已取消');
    const session: DeviceSession = {
      transport: 'webhid',
      productName: transport.session?.productName,
      productId: target.productId,
      pcbRevision: target.pcbRevision,
      webConfigProfile: target.webConfigProfile,
      webConfigBasePath: `/webconfig/${target.webConfigProfile}/`,
      hardwareVersion: reply.hardwareVersion,
      authenticated: true, // Internal transport-ready marker; no device identity is checked.
      scopes: ALL_SCOPES,
      sessionId: reply.sessionId,
    };
    transport.establishSecureSession(new AesGcmHidSessionCipher(keys), session);
    this.active = true;
    return session;
  }

  async reauthorize(
    transport: WebHidTransport,
    scopes: readonly DeviceScope[],
    signal?: AbortSignal,
  ): Promise<DeviceSession> {
    if (this.active && transport.session && this.hasScopes(scopes)) return transport.session;
    return this.authenticate(transport, scopes, signal);
  }

  async authorizedFetch(
    input: RequestInfo | URL,
    init: RequestInit = {},
    requiredScopes: readonly DeviceScope[] = [],
  ): Promise<Response> {
    if (!this.hasScopes(requiredScopes)) {
      throw new DeviceTransportError('authentication-required', '设备会话未连接');
    }
    const url = new URL(
      input instanceof Request ? input.url : String(input),
      globalThis.location.origin,
    );
    if (url.origin !== globalThis.location.origin) {
      throw new DeviceTransportError('permission-denied', '设备资源请求必须同源');
    }
    return fetch(url.href, { ...init, credentials: 'same-origin', redirect: 'error' });
  }

  hasScopes(scopes: readonly DeviceScope[]): boolean {
    return this.active && scopes.every((scope) => ALL_SCOPES.includes(scope));
  }

  clear(): void {
    this.active = false;
  }
}

function toBase64(bytes: Uint8Array): string {
  let binary = '';
  for (const value of bytes) binary += String.fromCharCode(value);
  return btoa(binary);
}

function fromBase64(value: string): Uint8Array {
  try {
    return Uint8Array.from(atob(value), (character) => character.charCodeAt(0));
  } catch (error) {
    throw new DeviceTransportError('protocol', '设备返回了无效的 Base64', error);
  }
}
