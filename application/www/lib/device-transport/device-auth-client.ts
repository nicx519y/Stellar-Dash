import { AesGcmHidSessionCipher, deriveBrowserSessionKeys } from './session-crypto';
import {
  DEFAULT_DEVICE_SCOPES,
  DeviceScope,
  DeviceSession,
  DeviceTransportError,
} from './types';
import { WebHidTransport } from './webhid-transport';

export interface DeviceAuthClientOptions {
  challengeEndpoint?: string;
  verifyEndpoint?: string;
  serverOrigin?: string;
  scopes?: readonly DeviceScope[];
  fetch?: typeof globalThis.fetch;
}

interface ServerChallenge {
  challengeId: string;
  nonce: string;
  expiresAt: number;
}

interface DeviceAttestation {
  deviceId: string;
  certificate: string;
  bootAttestation: string;
  bootNonce: string;
  deviceEphemeralPublicKey: string;
  firmwareMeasurement: string;
  hardwareVersion: string;
  firmwareVersion: string;
  signature: string;
}

interface ServerAuthorization {
  apiToken: string;
  expiresInMs: number;
  sessionId: string;
  deviceSessionPermit: string;
  sessionSalt: string;
  scopes: DeviceScope[];
}

interface PermitAck {
  accepted: boolean;
  sessionId: string;
}

export class DeviceAuthClient {
  private readonly challengeEndpoint: string;
  private readonly verifyEndpoint: string;
  private readonly authServerOrigin: string;
  private readonly defaultScopes: readonly DeviceScope[];
  private readonly fetchImpl: typeof globalThis.fetch;
  private apiToken: string | null = null;
  private apiTokenExpiresAt = 0;
  private grantedScopes: readonly DeviceScope[] = [];

  constructor(options: DeviceAuthClientOptions = {}) {
    const challengeEndpoint =
      options.challengeEndpoint ?? '/api/v2/device-auth/challenges';
    const verifyEndpoint =
      options.verifyEndpoint ?? '/api/v2/device-auth/verify';
    const runtimeOrigin =
      typeof globalThis.location?.origin === 'string'
        ? globalThis.location.origin
        : 'https://hbox-webconfig.invalid';
    this.authServerOrigin = new URL(
      options.serverOrigin || challengeEndpoint,
      runtimeOrigin,
    ).origin;
    const challengeUrl = new URL(challengeEndpoint, this.authServerOrigin);
    const verifyUrl = new URL(verifyEndpoint, this.authServerOrigin);
    if (
      challengeUrl.origin !== this.authServerOrigin ||
      verifyUrl.origin !== this.authServerOrigin
    ) {
      throw new DeviceTransportError(
        'protocol',
        'Device authentication endpoints must use one pinned server origin',
      );
    }
    this.challengeEndpoint = challengeUrl.href;
    this.verifyEndpoint = verifyUrl.href;
    this.defaultScopes = normalizeScopes(options.scopes ?? DEFAULT_DEVICE_SCOPES);
    this.fetchImpl = options.fetch ?? globalThis.fetch.bind(globalThis);
  }

  async authenticate(
    transport: WebHidTransport,
    scopes: readonly DeviceScope[] = this.defaultScopes,
  ): Promise<DeviceSession> {
    this.clear();
    const requestedScopes = normalizeScopes(scopes);
    transport.setAuthenticating();
    const browserKeyPair = await crypto.subtle.generateKey(
      { name: 'ECDH', namedCurve: 'P-256' },
      true,
      ['deriveBits'],
    );
    const browserPublicKey = new Uint8Array(
      await crypto.subtle.exportKey('raw', browserKeyPair.publicKey),
    );
    const challenge = await this.fetchChallenge(browserPublicKey, requestedScopes);
    validateChallenge(challenge);

    const attestation = await transport.bootstrapRequest<DeviceAttestation>(
      'attestation.create',
      {
        challengeId: challenge.challengeId,
        challengeNonce: challenge.nonce,
        browserEphemeralPublicKey: bytesToBase64(browserPublicKey),
        requestedScopes,
      },
    );
    validateAttestation(attestation);

    const authorization = await this.postJson<ServerAuthorization>(
      this.verifyEndpoint,
      {
        challengeId: challenge.challengeId,
        challengeNonce: challenge.nonce,
        browserEphemeralPublicKey: bytesToBase64(browserPublicKey),
        requestedScopes,
        deviceAttestation: attestation,
      },
    );
    validateAuthorization(authorization, requestedScopes);

    // The permit signature is deliberately verified by STM32, not trusted by
    // this page. A copied or malicious web page cannot authorize itself.
    const ack = await transport.bootstrapRequest<PermitAck>('session.install-permit', {
      sessionId: authorization.sessionId,
      permit: authorization.deviceSessionPermit,
    });
    if (!ack.accepted || ack.sessionId !== authorization.sessionId) {
      throw new DeviceTransportError('authentication-failed', '设备拒绝服务器签发的会话许可');
    }

    const sessionKeys = await deriveBrowserSessionKeys(
      browserKeyPair.privateKey,
      base64ToBytes(attestation.deviceEphemeralPublicKey),
      base64ToBytes(authorization.sessionSalt),
      authorization.sessionId,
    );
    const now = Date.now();
    const expiresInMs = Math.min(Math.max(authorization.expiresInMs, 1), 5 * 60 * 1000);
    const session: DeviceSession = {
      transport: 'webhid',
      deviceId: attestation.deviceId,
      productName: transport.session?.productName,
      hardwareVersion: attestation.hardwareVersion,
      firmwareVersion: attestation.firmwareVersion,
      authenticated: true,
      scopes: authorization.scopes,
      sessionId: authorization.sessionId,
      expiresAt: now + expiresInMs,
    };
    this.apiToken = authorization.apiToken;
    this.apiTokenExpiresAt = session.expiresAt!;
    this.grantedScopes = [...authorization.scopes];
    transport.establishSecureSession(new AesGcmHidSessionCipher(sessionKeys), session);
    return session;
  }

  async reauthorize(
    transport: WebHidTransport,
    scopes: readonly DeviceScope[],
  ): Promise<DeviceSession> {
    const requestedScopes = normalizeScopes(scopes);
    if (
      transport.session?.authenticated &&
      requestedScopes.every((scope) => transport.session!.scopes.includes(scope)) &&
      this.hasScopes(requestedScopes)
    ) {
      return transport.session;
    }

    this.clear();
    try {
      if (transport.session?.authenticated) {
        await transport.endSecureSessionForReauthorization();
      }
      return await this.authenticate(transport, requestedScopes);
    } catch (error) {
      this.clear();
      await transport.close().catch(() => undefined);
      throw error;
    }
  }

  async authorizedFetch(
    input: RequestInfo | URL,
    init: RequestInit = {},
    requiredScopes: readonly DeviceScope[] = [],
  ): Promise<Response> {
    if (!this.apiToken || Date.now() >= this.apiTokenExpiresAt) {
      this.clear();
      throw new DeviceTransportError('authentication-required', '服务器会话已过期，请重新连接设备');
    }
    const normalizedRequiredScopes = normalizeScopes(requiredScopes, true);
    if (!this.hasScopes(normalizedRequiredScopes)) {
      throw new DeviceTransportError(
        'authentication-required',
        '服务器令牌缺少此操作所需的设备权限',
      );
    }
    const resolved = new URL(requestUrl(input), this.authServerOrigin);
    if (resolved.origin !== this.authServerOrigin) {
      throw new DeviceTransportError(
        'permission-denied',
        '拒绝向非认证服务器 origin 发送设备授权令牌',
      );
    }
    const headers = new Headers(init.headers);
    headers.set('Authorization', `Bearer ${this.apiToken}`);
    const pinnedInput = typeof input === 'object' && !(input instanceof URL) && 'url' in input
      ? input
      : resolved.href;
    return this.fetchImpl(pinnedInput, {
      ...init,
      headers,
      credentials: 'omit',
      redirect: 'error',
    });
  }

  clear(): void {
    this.apiToken = null;
    this.apiTokenExpiresAt = 0;
    this.grantedScopes = [];
  }

  hasScopes(scopes: readonly DeviceScope[]): boolean {
    return scopes.every((scope) => this.grantedScopes.includes(scope));
  }

  private fetchChallenge(
    browserPublicKey: Uint8Array,
    requestedScopes: readonly DeviceScope[],
  ): Promise<ServerChallenge> {
    return this.postJson<ServerChallenge>(this.challengeEndpoint, {
      protocol: 'hbox-webhid-v1',
      requestedScopes,
      browserPublicKey: bytesToBase64(browserPublicKey),
    });
  }

  private async postJson<T>(url: string, body: unknown): Promise<T> {
    let response: Response;
    try {
      response = await this.fetchImpl(url, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(body),
        credentials: 'omit',
        cache: 'no-store',
        redirect: 'error',
      });
    } catch (error) {
      throw new DeviceTransportError('server', '无法连接设备认证服务器', error);
    }
    if (!response.ok) {
      throw new DeviceTransportError('server', `设备认证服务器返回 HTTP ${response.status}`);
    }
    try {
      return await response.json() as T;
    } catch (error) {
      throw new DeviceTransportError('server', '设备认证服务器返回了无效 JSON', error);
    }
  }
}

function validateChallenge(value: ServerChallenge): void {
  if (
    !value ||
    !isOpaqueIdentifier(value.challengeId) ||
    base64ToBytes(value.nonce).byteLength !== 32 ||
    !Number.isFinite(value.expiresAt) ||
    value.expiresAt <= Date.now()
  ) {
    throw new DeviceTransportError('authentication-failed', '认证服务器返回了无效 challenge');
  }
}

function validateAttestation(value: DeviceAttestation): void {
  if (
    !value ||
    !isOpaqueIdentifier(value.deviceId) ||
    !value.certificate ||
    !value.bootAttestation ||
    !value.bootNonce ||
    base64ToBytes(value.deviceEphemeralPublicKey).byteLength !== 65 ||
    !value.firmwareMeasurement ||
    !value.hardwareVersion ||
    !value.firmwareVersion ||
    !value.signature
  ) {
    throw new DeviceTransportError('authentication-failed', '设备证明响应缺少必要字段');
  }
}

function validateAuthorization(
  value: ServerAuthorization,
  requestedScopes: readonly DeviceScope[],
): void {
  if (
    !value ||
    !value.apiToken ||
    !isOpaqueIdentifier(value.sessionId) ||
    !value.deviceSessionPermit ||
    base64ToBytes(value.sessionSalt).byteLength < 16 ||
    !Number.isFinite(value.expiresInMs) ||
    value.expiresInMs <= 0 ||
    value.expiresInMs > 5 * 60 * 1000 ||
    !Array.isArray(value.scopes) ||
    value.scopes.length !== requestedScopes.length ||
    value.scopes.some((scope) => !requestedScopes.includes(scope)) ||
    requestedScopes.some((scope) => !value.scopes.includes(scope))
  ) {
    throw new DeviceTransportError('authentication-failed', '认证服务器返回了无效会话许可');
  }
}

function isOpaqueIdentifier(value: unknown): value is string {
  return typeof value === 'string' && value.length >= 8 && value.length <= 256;
}

function base64ToBytes(value: string): Uint8Array {
  try {
    const binary = atob(value);
    return Uint8Array.from(binary, (character) => character.charCodeAt(0));
  } catch (error) {
    throw new DeviceTransportError('authentication-failed', '认证字段不是有效 Base64', error);
  }
}

function bytesToBase64(bytes: Uint8Array): string {
  let binary = '';
  for (const value of bytes) binary += String.fromCharCode(value);
  return btoa(binary);
}

function normalizeScopes(
  scopes: readonly DeviceScope[],
  allowEmpty = false,
): readonly DeviceScope[] {
  const known = new Set<DeviceScope>([
    'config.read',
    'config.write',
    'monitor.read',
    'device.control',
    'asset.write',
    'firmware.update',
  ]);
  const normalized: DeviceScope[] = [];
  for (const scope of scopes) {
    if (!known.has(scope)) {
      throw new DeviceTransportError('protocol', `Unknown device scope ${scope}`);
    }
    if (!normalized.includes(scope)) {
      normalized.push(scope);
    }
  }
  if (!allowEmpty && normalized.length === 0) {
    throw new DeviceTransportError('protocol', 'At least one device scope is required');
  }
  return normalized;
}

function requestUrl(input: RequestInfo | URL): string {
  if (input instanceof URL) {
    return input.href;
  }
  if (typeof input === 'string') {
    return input;
  }
  return input.url;
}
