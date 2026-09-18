import { AesGcmHidSessionCipher, deriveBrowserSessionKeys } from './session-crypto';
import {
  DEFAULT_DEVICE_SCOPES,
  DeviceScope,
  DeviceSession,
  DeviceTransportError,
} from './types';
import { WebHidTransport } from './webhid-transport';
import {
  resolveAuthenticatedWebConfigTarget,
  ServerWebConfigTarget,
} from './webconfig-target';

export interface DeviceAuthClientOptions {
  challengeEndpoint?: string;
  verifyEndpoint?: string;
  serverOrigin?: string;
  scopes?: readonly DeviceScope[];
  fetch?: typeof globalThis.fetch;
  httpTimeoutMs?: number;
}

const DEFAULT_AUTH_HTTP_TIMEOUT_MS = 10_000;
// Authentication bootstrap is a logical WebHID RPC too.  In particular,
// session.install-permit is fragmented and its deadline covers the write
// queue, every native sendReport(), and the complete response.  Bootstrap uses
// a dedicated 10-second budget so a recoverable stale-generation retry can
// still finish near the 15-second readiness target. Ordinary RPCs retain their
// 15-second deadline and DeviceCommandClient retains the 30-second hard bound.
const BOOTSTRAP_STEP_TIMEOUT_MS = 10_000;

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

interface ServerAuthorization extends ServerWebConfigTarget {
  apiToken: string;
  accountUid: string;
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
  private readonly httpTimeoutMs: number;
  private apiToken: string | null = null;
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
    this.httpTimeoutMs = options.httpTimeoutMs ?? DEFAULT_AUTH_HTTP_TIMEOUT_MS;
  }

  async authenticate(
    transport: WebHidTransport,
    scopes: readonly DeviceScope[] = this.defaultScopes,
    signal?: AbortSignal,
    onAuthorizing?: () => void,
  ): Promise<DeviceSession> {
    assertAuthenticationActive(signal);
    this.clear();
    const requestedScopes = normalizeScopes(scopes);
    transport.setAuthenticating();
    const browserKeyPair = await crypto.subtle.generateKey(
      { name: 'ECDH', namedCurve: 'P-256' },
      true,
      ['deriveBits'],
    );
    assertAuthenticationActive(signal);
    const browserPublicKey = new Uint8Array(
      await crypto.subtle.exportKey('raw', browserKeyPair.publicKey),
    );
    assertAuthenticationActive(signal);
    const challenge = await this.fetchChallenge(browserPublicKey, requestedScopes, signal);
    assertAuthenticationActive(signal);
    validateChallenge(challenge);

    const attestation = await transport.bootstrapRequest<DeviceAttestation>(
      'attestation.create',
      {
        challengeId: challenge.challengeId,
        challengeNonce: challenge.nonce,
        browserEphemeralPublicKey: bytesToBase64(browserPublicKey),
        requestedScopes,
      },
      { signal, timeoutMs: BOOTSTRAP_STEP_TIMEOUT_MS },
    );
    assertAuthenticationActive(signal);
    validateAttestation(attestation);
    onAuthorizing?.();
    assertAuthenticationActive(signal);

    const authorization = await this.postJson<ServerAuthorization>(
      this.verifyEndpoint,
      {
        challengeId: challenge.challengeId,
        challengeNonce: challenge.nonce,
        browserEphemeralPublicKey: bytesToBase64(browserPublicKey),
        requestedScopes,
        deviceAttestation: attestation,
      },
      signal,
    );
    assertAuthenticationActive(signal);
    const webConfigTarget = validateAuthorization(
      authorization,
      requestedScopes,
      attestation.hardwareVersion,
    );

    // The permit signature is deliberately verified by STM32, not trusted by
    // this page. A copied or malicious web page cannot authorize itself.
    const ack = await transport.bootstrapRequest<PermitAck>(
      'session.install-permit',
      {
        sessionId: authorization.sessionId,
        permit: authorization.deviceSessionPermit,
      },
      { signal, timeoutMs: BOOTSTRAP_STEP_TIMEOUT_MS },
    );
    assertAuthenticationActive(signal);
    if (!ack.accepted || ack.sessionId !== authorization.sessionId) {
      throw new DeviceTransportError('authentication-failed', '设备拒绝服务器签发的会话许可');
    }

    const sessionKeys = await deriveBrowserSessionKeys(
      browserKeyPair.privateKey,
      base64ToBytes(attestation.deviceEphemeralPublicKey),
      base64ToBytes(authorization.sessionSalt),
      authorization.sessionId,
    );
    assertAuthenticationActive(signal);
    const session: DeviceSession = {
      transport: 'webhid',
      deviceId: attestation.deviceId,
      accountUid: authorization.accountUid,
      productName: transport.session?.productName,
      productId: webConfigTarget.productId,
      pcbRevision: webConfigTarget.pcbRevision,
      webConfigProfile: webConfigTarget.webConfigProfile,
      webConfigBasePath: webConfigTarget.basePath,
      hardwareVersion: webConfigTarget.pcbRevision,
      firmwareVersion: attestation.firmwareVersion,
      authenticated: true,
      scopes: authorization.scopes,
      sessionId: authorization.sessionId,
    };
    this.apiToken = authorization.apiToken;
    this.grantedScopes = [...authorization.scopes];
    transport.establishSecureSession(new AesGcmHidSessionCipher(sessionKeys), session);
    return session;
  }

  async reauthorize(
    transport: WebHidTransport,
    scopes: readonly DeviceScope[],
    signal?: AbortSignal,
  ): Promise<DeviceSession> {
    assertAuthenticationActive(signal);
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
        assertAuthenticationActive(signal);
      }
      const session = await this.authenticate(transport, requestedScopes, signal);
      assertAuthenticationActive(signal);
      return session;
    } catch (error) {
      // A superseded authentication flow must not clear or close a transport
      // that may already belong to the next adapter lifecycle generation.
      assertAuthenticationActive(signal);
      this.clear();
      await transport.close().catch(() => undefined);
      assertAuthenticationActive(signal);
      throw error;
    }
  }

  async authorizedFetch(
    input: RequestInfo | URL,
    init: RequestInit = {},
    requiredScopes: readonly DeviceScope[] = [],
  ): Promise<Response> {
    if (!this.apiToken) {
      this.clear();
      throw new DeviceTransportError('authentication-required', '服务器会话不可用，请重新连接设备');
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
      // Requests are pinned to authServerOrigin above.  Same-origin cookies
      // are required only for administrator switch-mapping publication, where
      // the server deliberately requires both the device bearer session and
      // the administrator email session.
      credentials: 'same-origin',
      redirect: 'error',
    });
  }

  clear(): void {
    this.apiToken = null;
    this.grantedScopes = [];
  }

  hasScopes(scopes: readonly DeviceScope[]): boolean {
    return scopes.every((scope) => this.grantedScopes.includes(scope));
  }

  private fetchChallenge(
    browserPublicKey: Uint8Array,
    requestedScopes: readonly DeviceScope[],
    signal?: AbortSignal,
  ): Promise<ServerChallenge> {
    return this.postJson<ServerChallenge>(this.challengeEndpoint, {
      protocol: 'hbox-webhid-v1',
      requestedScopes,
      browserPublicKey: bytesToBase64(browserPublicKey),
    }, signal);
  }

  private async postJson<T>(
    url: string,
    body: unknown,
    signal?: AbortSignal,
  ): Promise<T> {
    assertAuthenticationActive(signal);
    if (!Number.isFinite(this.httpTimeoutMs) || this.httpTimeoutMs <= 0) {
      throw new DeviceTransportError('protocol', 'Authentication HTTP timeout must be positive');
    }
    const deadline = Date.now() + this.httpTimeoutMs;
    const requestController = new AbortController();
    let response: Response;
    try {
      const request = Promise.resolve(this.fetchImpl(url, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(body),
        credentials: 'omit',
        cache: 'no-store',
        redirect: 'error',
        signal: requestController.signal,
      }));
      response = await this.awaitHttpOperation(
        request,
        deadline,
        requestController,
        signal,
        '设备认证服务器请求',
      );
    } catch (error) {
      assertAuthenticationActive(signal);
      if (error instanceof DeviceTransportError) throw error;
      throw new DeviceTransportError('server', '无法连接设备认证服务器', error);
    }
    assertAuthenticationActive(signal);
    if (!response.ok) {
      throw new DeviceTransportError('server', `设备认证服务器返回 HTTP ${response.status}`);
    }
    try {
      const value = await this.awaitHttpOperation(
        Promise.resolve(response.json() as Promise<T>),
        deadline,
        requestController,
        signal,
        '设备认证服务器响应',
      );
      assertAuthenticationActive(signal);
      return value;
    } catch (error) {
      assertAuthenticationActive(signal);
      if (error instanceof DeviceTransportError) throw error;
      throw new DeviceTransportError('server', '设备认证服务器返回了无效 JSON', error);
    }
  }

  private awaitHttpOperation<T>(
    operation: Promise<T>,
    deadline: number,
    controller: AbortController,
    signal: AbortSignal | undefined,
    label: string,
  ): Promise<T> {
    if (signal?.aborted) {
      controller.abort(signal.reason);
      return Promise.reject(new DeviceTransportError(
        'disconnected',
        '设备认证流程已被后续的断开或重连操作取消',
      ));
    }
    return new Promise<T>((resolve, reject) => {
      let settled = false;
      const finish = (callback: () => void): void => {
        if (settled) return;
        settled = true;
        clearTimeout(timeout);
        signal?.removeEventListener('abort', abort);
        callback();
      };
      const abort = (): void => {
        controller.abort(signal?.reason);
        finish(() => reject(new DeviceTransportError(
          'disconnected',
          '设备认证流程已被后续的断开或重连操作取消',
        )));
      };
      const timeout = setTimeout(() => {
        controller.abort();
        finish(() => reject(new DeviceTransportError(
          'timeout',
          `${label}超时`,
        )));
      }, Math.max(0, deadline - Date.now()));
      signal?.addEventListener('abort', abort, { once: true });
      operation.then(
        (value) => finish(() => resolve(value)),
        (error) => finish(() => reject(error)),
      );
    });
  }
}

function assertAuthenticationActive(signal?: AbortSignal): void {
  if (signal?.aborted) {
    throw new DeviceTransportError(
      'disconnected',
      '设备认证流程已被后续的断开或重连操作取消',
    );
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
  attestedHardwareVersion: string,
) {
  if (
    !value ||
    !value.apiToken ||
    !isUuidV4(value.accountUid) ||
    !isOpaqueIdentifier(value.sessionId) ||
    !value.deviceSessionPermit ||
    base64ToBytes(value.sessionSalt).byteLength < 16 ||
    !Array.isArray(value.scopes) ||
    value.scopes.length !== requestedScopes.length ||
    value.scopes.some((scope) => !requestedScopes.includes(scope)) ||
    requestedScopes.some((scope) => !value.scopes.includes(scope))
  ) {
    throw new DeviceTransportError('authentication-failed', '认证服务器返回了无效会话许可');
  }
  return resolveAuthenticatedWebConfigTarget(value, attestedHardwareVersion);
}

function isUuidV4(value: unknown): value is string {
  return typeof value === 'string' &&
    /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/i.test(value);
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
