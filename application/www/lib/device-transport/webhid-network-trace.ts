import {
  SecureHidFrameFlags,
  SecureHidFrameType,
} from './secure-hid-frame';

export type WebHidNetworkTraceMode = 'control' | 'all';
export type WebHidTraceDirection = 'tx' | 'rx';
export type WebHidTraceStatus =
  | 'pending'
  | 'success'
  | 'failed'
  | 'event'
  | 'captured';

const TRACE_QUERY_PARAMETER = 'webhidNetworkTrace';
const TRACE_ENDPOINT_PREFIX = '/__hbox_webhid_trace__/';
const TRACE_SERVICE_WORKER_PATH = '/webhid-network-trace-sw.js';
const TRACE_CHANNEL_NAME = 'hbox-webhid-trace-v1';
const TRACE_CHANNEL_PROTOCOL = 'hbox-webhid-trace-v1';
const TRACE_VIEWER_HEARTBEAT_MS = 3_000;
const TRACE_VIEWER_LEASE_MS = 15_000;
const MAX_PENDING_TRACE_REQUESTS = 512;
const MAX_MUTABLE_TRACE_RECORDS = 2_048;

const FRAME_TYPE_NAMES: Readonly<Record<number, string>> = {
  [SecureHidFrameType.BOOTSTRAP_REQUEST]: 'BOOTSTRAP_REQUEST',
  [SecureHidFrameType.BOOTSTRAP_RESPONSE]: 'BOOTSTRAP_RESPONSE',
  [SecureHidFrameType.RPC_REQUEST]: 'RPC_REQUEST',
  [SecureHidFrameType.RPC_RESPONSE]: 'RPC_RESPONSE',
  [SecureHidFrameType.EVENT]: 'EVENT',
  [SecureHidFrameType.PERF_SAMPLE]: 'PERF_SAMPLE',
  [SecureHidFrameType.PERF_EDGE]: 'PERF_EDGE',
  [SecureHidFrameType.PERF_CHECKPOINT]: 'PERF_CHECKPOINT',
  [SecureHidFrameType.STREAM_CHUNK]: 'STREAM_CHUNK',
  [SecureHidFrameType.ERROR]: 'ERROR',
};

const PERFORMANCE_FRAME_TYPES = new Set<SecureHidFrameType>([
  SecureHidFrameType.PERF_SAMPLE,
  SecureHidFrameType.PERF_EDGE,
  SecureHidFrameType.PERF_CHECKPOINT,
]);

interface PendingTraceRequest {
  url: string;
  body: string;
}

interface ViewerLease {
  mode: WebHidNetworkTraceMode;
  expiresAt: number;
}

interface TraceControlMessage {
  protocol: typeof TRACE_CHANNEL_PROTOCOL;
  kind: 'trace-control';
  viewerId: string;
  mode: WebHidNetworkTraceMode;
}

interface TraceRecordMessage {
  protocol: typeof TRACE_CHANNEL_PROTOCOL;
  kind: 'trace-record';
  record: WebHidTraceRecord;
}

interface TraceSourceStatusMessage {
  protocol: typeof TRACE_CHANNEL_PROTOCOL;
  kind: 'source-status';
  source: WebHidTraceSource;
}

type TraceChannelMessage =
  | TraceControlMessage
  | TraceRecordMessage
  | TraceSourceStatusMessage;

export interface WebHidFrameTraceInput {
  direction: WebHidTraceDirection;
  reportId: number;
  type: SecureHidFrameType;
  flags: number;
  sequence: number;
  secure: boolean;
  plaintextPayload: Uint8Array;
  wireReport: Uint8Array;
  logicalRecordId?: string | null;
}

export interface WebHidLogicalTraceInput {
  direction: WebHidTraceDirection;
  type: SecureHidFrameType;
  secure: boolean;
  plaintextPayload: Uint8Array;
  transactionId?: number;
  command?: string;
  decoded?: unknown;
  status?: WebHidTraceStatus;
  errorCode?: string;
  errorMessage?: string;
  frameRecordIds?: string[];
}

export interface WebHidLogicalTraceUpdate {
  status: 'success' | 'failed';
  responseDecoded?: unknown;
  errorCode?: string;
  errorMessage?: string;
  responseFrameRecordIds?: string[];
}

export interface WebHidTraceRecord extends Record<string, unknown> {
  recordId: string;
  captureSequence: number;
  sourceId: string;
  sourceUrl: string;
  name: string;
  traceKind: 'frame' | 'logical';
  direction: WebHidTraceDirection;
  typeName: string;
  capturedAt: string;
}

export interface WebHidTraceSource {
  sourceId: string;
  pageUrl: string;
  mode: WebHidNetworkTraceMode;
  reportedAt: string;
}

export interface WebHidTraceViewerSession {
  readonly supported: boolean;
  setMode: (mode: WebHidNetworkTraceMode) => void;
  close: () => void;
}

export interface WebHidTraceViewerCallbacks {
  onRecord: (record: WebHidTraceRecord) => void;
  onSource: (source: WebHidTraceSource) => void;
}

export function upsertWebHidTraceRecord(
  records: WebHidTraceRecord[],
  record: WebHidTraceRecord,
  maximumRecords = 2_000,
): WebHidTraceRecord[] {
  const index = records.findIndex(
    (candidate) => candidate.recordId === record.recordId,
  );
  if (index >= 0) {
    const next = records.slice();
    next[index] = record;
    return next;
  }
  const next = [...records, record];
  return next.length > maximumRecords
    ? next.slice(next.length - maximumRecords)
    : next;
}

export function relatedWebHidTraceFrames(
  records: WebHidTraceRecord[],
  logicalRecord: WebHidTraceRecord | null,
): WebHidTraceRecord[] {
  if (!logicalRecord || logicalRecord.traceKind !== 'logical') return [];
  const linkedIds = new Set<string>();
  for (const value of [logicalRecord.frameRecordIds, logicalRecord.responseFrameRecordIds]) {
    if (!Array.isArray(value)) continue;
    value.forEach((recordId) => {
      if (typeof recordId === 'string') linkedIds.add(recordId);
    });
  }
  return records
    .filter((record) => (
      record.traceKind === 'frame'
      && (record.logicalRecordId === logicalRecord.recordId || linkedIds.has(record.recordId))
    ))
    .sort((left, right) => left.captureSequence - right.captureSequence);
}

// Capture the opt-in before the index route redirects to /global/ and drops
// its query string. Browser and static-render module instances are separate,
// so the server-side undefined value cannot enable tracing in the client.
const initialBrowserSearch = typeof window === 'undefined'
  ? null
  : window.location.search;
let configuredMode: WebHidNetworkTraceMode | null | undefined =
  initialBrowserSearch === null
    ? undefined
    : resolveWebHidNetworkTraceMode(
      initialBrowserSearch,
      process.env.NODE_ENV === 'development',
      isLocalWebHidTraceHostname(window.location.hostname),
    );
let registrationStarted = false;
let traceSequence = 1;
let warnedUnavailable = false;
let sourceChannel: BroadcastChannel | null = null;
let sourceId: string | null = null;
const viewerLeases = new Map<string, ViewerLease>();
const pendingTraceRequests: PendingTraceRequest[] = [];
const mutableTraceRecords = new Map<string, WebHidTraceRecord>();
const utf8Decoder = new TextDecoder('utf-8', { fatal: true });

export function isLocalWebHidTraceHostname(hostname: string): boolean {
  const normalized = hostname.trim().toLowerCase();
  return normalized === 'localhost' ||
    normalized === '127.0.0.1' ||
    normalized === '::1' ||
    normalized === '[::1]';
}

export function resolveWebHidNetworkTraceMode(
  search: string,
  development = process.env.NODE_ENV === 'development',
  localTraceAllowed = false,
): WebHidNetworkTraceMode | null {
  if (!development && !localTraceAllowed) {
    return null;
  }
  const requested = new URLSearchParams(search).get(TRACE_QUERY_PARAMETER);
  return requested === 'control' || requested === 'all' ? requested : null;
}

/**
 * Prepares a WebConfig tab to publish traces. The dedicated viewer activates
 * capture through a same-origin BroadcastChannel lease, so a viewer tab never
 * opens the HID device or competes for the WebHID connection lock.
 */
export function initializeWebHidNetworkTrace(): void {
  if (isLocalTraceRuntime()) {
    ensureTraceSourceChannel();
  }
  if (currentTraceMode()) {
    ensureTraceServiceWorker();
  }
}

/** Open the local-only receiver used by /webhid-trace/. */
export function openWebHidTraceViewer(
  initialMode: WebHidNetworkTraceMode,
  callbacks: WebHidTraceViewerCallbacks,
): WebHidTraceViewerSession {
  if (
    typeof window === 'undefined' ||
    typeof BroadcastChannel === 'undefined' ||
    !isLocalTraceRuntime()
  ) {
    return {
      supported: false,
      setMode: () => undefined,
      close: () => undefined,
    };
  }

  const channel = new BroadcastChannel(TRACE_CHANNEL_NAME);
  const viewerId = createTracePeerId('viewer');
  let mode = initialMode;
  let closed = false;

  channel.onmessage = (event: MessageEvent<unknown>) => {
    const message = parseTraceChannelMessage(event.data);
    if (!message) return;
    if (message.kind === 'trace-record') {
      callbacks.onRecord(message.record);
    } else if (message.kind === 'source-status') {
      callbacks.onSource(message.source);
    }
  };

  const sendControl = () => {
    if (closed) return;
    const message: TraceControlMessage = {
      protocol: TRACE_CHANNEL_PROTOCOL,
      kind: 'trace-control',
      viewerId,
      mode,
    };
    channel.postMessage(message);
  };
  sendControl();
  const heartbeat = window.setInterval(sendControl, TRACE_VIEWER_HEARTBEAT_MS);

  return {
    supported: true,
    setMode(nextMode) {
      mode = nextMode;
      sendControl();
    },
    close() {
      if (closed) return;
      closed = true;
      window.clearInterval(heartbeat);
      channel.close();
    },
  };
}

export function traceWebHidFrame(input: WebHidFrameTraceInput): string | null {
  const mode = currentTraceMode();
  if (!mode || (mode === 'control' && PERFORMANCE_FRAME_TYPES.has(input.type))) {
    return null;
  }

  const typeName = frameTypeName(input.type);
  const effectiveFlags = input.flags |
    (input.secure ? SecureHidFrameFlags.SECURE : 0);
  const record = emitTraceRecord(
    `${input.direction.toUpperCase()}-FRAME-${typeName}-SEQ-${input.sequence}`,
    {
      traceKind: 'frame',
      direction: input.direction,
      reportId: input.reportId,
      version: input.wireReport[0],
      type: input.type,
      typeName,
      flags: effectiveFlags,
      flagNames: frameFlagNames(effectiveFlags),
      sequence: input.sequence,
      secure: input.secure,
      payloadLength: input.plaintextPayload.byteLength,
      plaintextHex: bytesToHex(input.plaintextPayload),
      plaintextUtf8: decodeReadableUtf8(input.plaintextPayload),
      wireHex: bytesToHex(input.wireReport),
      logicalRecordId: input.logicalRecordId ?? undefined,
      capturedAt: new Date().toISOString(),
      monotonicTimeMs: monotonicTime(),
    },
  );
  return record?.recordId ?? null;
}

export function traceWebHidLogical(
  input: WebHidLogicalTraceInput,
): string | null {
  const mode = currentTraceMode();
  if (!mode || (mode === 'control' && PERFORMANCE_FRAME_TYPES.has(input.type))) {
    return null;
  }

  const typeName = frameTypeName(input.type);
  const command = input.command ? sanitizePathSegment(input.command) : 'UNKNOWN';
  const transaction = input.transactionId === undefined
    ? 'NA'
    : String(input.transactionId);
  const record = emitTraceRecord(
    `${input.direction.toUpperCase()}-LOGICAL-${command}-TID-${transaction}`,
    {
      traceKind: 'logical',
      direction: input.direction,
      type: input.type,
      typeName,
      secure: input.secure,
      transactionId: input.transactionId,
      command: input.command,
      decoded: input.decoded,
      status: input.status ?? (input.direction === 'tx' ? 'pending' : 'success'),
      errorCode: input.errorCode,
      errorMessage: input.errorMessage,
      frameRecordIds: input.frameRecordIds,
      plaintextLength: input.plaintextPayload.byteLength,
      plaintextHex: bytesToHex(input.plaintextPayload),
      plaintextUtf8: decodeReadableUtf8(input.plaintextPayload),
      capturedAt: new Date().toISOString(),
      monotonicTimeMs: monotonicTime(),
    },
  );
  return record?.recordId ?? null;
}

/** Update the original logical request once a response or transport failure is known. */
export function updateWebHidLogicalTrace(
  recordId: string | null,
  update: WebHidLogicalTraceUpdate,
): void {
  if (!recordId) return;
  const current = mutableTraceRecords.get(recordId);
  if (!current) return;
  const record = {
    ...current,
    ...update,
    completedAt: new Date().toISOString(),
  } as WebHidTraceRecord;
  mutableTraceRecords.set(recordId, record);
  publishTraceRecord(record);
}

function currentTraceMode(): WebHidNetworkTraceMode | null {
  if (configuredMode === undefined) {
    configuredMode = typeof window === 'undefined'
      ? null
      : resolveWebHidNetworkTraceMode(
        window.location.search,
        process.env.NODE_ENV === 'development',
        isLocalWebHidTraceHostname(window.location.hostname),
      );
  }
  if (configuredMode) {
    return configuredMode;
  }

  const now = Date.now();
  let viewerMode: WebHidNetworkTraceMode | null = null;
  for (const [viewerId, lease] of viewerLeases) {
    if (lease.expiresAt <= now) {
      viewerLeases.delete(viewerId);
      continue;
    }
    if (lease.mode === 'all') return 'all';
    viewerMode = 'control';
  }
  return viewerMode;
}

function ensureTraceSourceChannel(): void {
  if (
    sourceChannel ||
    typeof BroadcastChannel === 'undefined' ||
    !isLocalTraceRuntime()
  ) {
    return;
  }

  sourceChannel = new BroadcastChannel(TRACE_CHANNEL_NAME);
  sourceId = createTracePeerId('source');
  sourceChannel.onmessage = (event: MessageEvent<unknown>) => {
    const message = parseTraceChannelMessage(event.data);
    if (!message || message.kind !== 'trace-control') return;
    viewerLeases.set(message.viewerId, {
      mode: message.mode,
      expiresAt: Date.now() + TRACE_VIEWER_LEASE_MS,
    });
    ensureTraceServiceWorker();
    publishSourceStatus(message.mode);
  };
}

function publishSourceStatus(mode: WebHidNetworkTraceMode): void {
  if (!sourceChannel || !sourceId || typeof window === 'undefined') return;
  const message: TraceSourceStatusMessage = {
    protocol: TRACE_CHANNEL_PROTOCOL,
    kind: 'source-status',
    source: {
      sourceId,
      pageUrl: window.location.href,
      mode,
      reportedAt: new Date().toISOString(),
    },
  };
  sourceChannel.postMessage(message);
}

function ensureTraceServiceWorker(): void {
  if (registrationStarted) {
    return;
  }
  registrationStarted = true;

  if (typeof navigator === 'undefined' || !('serviceWorker' in navigator)) {
    warnUnavailable('Service Worker is unavailable');
    return;
  }

  navigator.serviceWorker.addEventListener('controllerchange', flushPendingTraceRequests);
  void navigator.serviceWorker.register(TRACE_SERVICE_WORKER_PATH, { scope: '/' })
    .then(() => navigator.serviceWorker.ready)
    .then(() => {
      console.info(
        `[HBox WebHID] Network trace enabled (${currentTraceMode()}); ` +
        `filter DevTools Network by ${TRACE_ENDPOINT_PREFIX}`,
      );
      flushPendingTraceRequests();
    })
    .catch((error: unknown) => {
      warnUnavailable('failed to register the trace Service Worker', error);
    });
}

function emitTraceRecord(
  name: string,
  payload: Record<string, unknown>,
): WebHidTraceRecord | null {
  ensureTraceSourceChannel();
  ensureTraceServiceWorker();
  const captureSequence = traceSequence++;
  const requestNumber = String(captureSequence).padStart(6, '0');
  const activeSourceId = sourceId ?? createTracePeerId('source');
  if (!sourceId) sourceId = activeSourceId;
  const record = {
    recordId: `${activeSourceId}:${requestNumber}`,
    captureSequence,
    sourceId: activeSourceId,
    sourceUrl: typeof window === 'undefined' ? '' : window.location.href,
    name,
    ...payload,
  } as WebHidTraceRecord;
  mutableTraceRecords.set(record.recordId, record);
  while (mutableTraceRecords.size > MAX_MUTABLE_TRACE_RECORDS) {
    const oldest = mutableTraceRecords.keys().next().value as string | undefined;
    if (!oldest) break;
    mutableTraceRecords.delete(oldest);
  }
  const body = JSON.stringify(record, null, 2);

  publishTraceRecord(record);

  const request: PendingTraceRequest = {
    url: `${TRACE_ENDPOINT_PREFIX}${requestNumber}-${sanitizePathSegment(name)}`,
    body,
  };

  if (!isTraceServiceWorkerControlling()) {
    if (pendingTraceRequests.length >= MAX_PENDING_TRACE_REQUESTS) {
      pendingTraceRequests.shift();
    }
    pendingTraceRequests.push(request);
    return record;
  }
  sendTraceRequest(request);
  return record;
}

function publishTraceRecord(record: WebHidTraceRecord): void {
  if (!sourceChannel) return;
  const message: TraceRecordMessage = {
    protocol: TRACE_CHANNEL_PROTOCOL,
    kind: 'trace-record',
    record,
  };
  sourceChannel.postMessage(message);
}

function flushPendingTraceRequests(): void {
  if (!isTraceServiceWorkerControlling()) {
    return;
  }
  while (pendingTraceRequests.length !== 0) {
    sendTraceRequest(pendingTraceRequests.shift()!);
  }
}

function sendTraceRequest(request: PendingTraceRequest): void {
  void fetch(request.url, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
      'X-HBox-WebHID-Trace': '1',
    },
    body: request.body,
    cache: 'no-store',
    credentials: 'omit',
  }).catch((error: unknown) => {
    warnUnavailable('failed to mirror a WebHID trace record', error);
  });
}

function isTraceServiceWorkerControlling(): boolean {
  if (typeof navigator === 'undefined' || !('serviceWorker' in navigator)) {
    return false;
  }
  const controller = navigator.serviceWorker.controller;
  if (!controller) {
    return false;
  }
  try {
    return new URL(controller.scriptURL).pathname === TRACE_SERVICE_WORKER_PATH;
  } catch {
    return false;
  }
}

function parseTraceChannelMessage(value: unknown): TraceChannelMessage | null {
  if (!value || typeof value !== 'object') return null;
  const candidate = value as Partial<TraceChannelMessage>;
  if (candidate.protocol !== TRACE_CHANNEL_PROTOCOL) return null;

  if (candidate.kind === 'trace-control') {
    const control = candidate as Partial<TraceControlMessage>;
    if (
      typeof control.viewerId === 'string' &&
      (control.mode === 'control' || control.mode === 'all')
    ) {
      return control as TraceControlMessage;
    }
  } else if (candidate.kind === 'trace-record') {
    const traceRecord = candidate as Partial<TraceRecordMessage>;
    if (traceRecord.record && typeof traceRecord.record === 'object') {
      return traceRecord as TraceRecordMessage;
    }
  } else if (candidate.kind === 'source-status') {
    const status = candidate as Partial<TraceSourceStatusMessage>;
    if (status.source && typeof status.source === 'object') {
      return status as TraceSourceStatusMessage;
    }
  }
  return null;
}

function isLocalTraceRuntime(): boolean {
  return typeof window !== 'undefined' &&
    isLocalWebHidTraceHostname(window.location.hostname);
}

function createTracePeerId(prefix: string): string {
  const randomId = typeof crypto !== 'undefined' &&
    typeof crypto.randomUUID === 'function'
    ? crypto.randomUUID()
    : `${Date.now().toString(36)}-${Math.random().toString(36).slice(2)}`;
  return `${prefix}-${randomId}`;
}

function frameTypeName(type: SecureHidFrameType): string {
  return FRAME_TYPE_NAMES[type] ?? `TYPE_0x${type.toString(16).padStart(2, '0')}`;
}

function frameFlagNames(flags: number): string[] {
  const names: string[] = [];
  if ((flags & SecureHidFrameFlags.SECURE) !== 0) names.push('SECURE');
  if ((flags & SecureHidFrameFlags.FRAGMENTED) !== 0) names.push('FRAGMENTED');
  if ((flags & SecureHidFrameFlags.LAST) !== 0) names.push('LAST');
  if ((flags & SecureHidFrameFlags.ACK_REQUIRED) !== 0) names.push('ACK_REQUIRED');
  return names;
}

function bytesToHex(bytes: Uint8Array): string {
  return Array.from(bytes, (value) => value.toString(16).padStart(2, '0')).join(' ');
}

function decodeReadableUtf8(bytes: Uint8Array): string | null {
  if (bytes.byteLength === 0) {
    return '';
  }
  try {
    const decoded = utf8Decoder.decode(bytes);
    return /^(?:[\t\n\r\x20-\x7e]|[^\x00-\x1f\x7f])*$/.test(decoded)
      ? decoded
      : null;
  } catch {
    return null;
  }
}

function sanitizePathSegment(value: string): string {
  const sanitized = value.replace(/[^a-zA-Z0-9._-]+/g, '_').slice(0, 80);
  return sanitized || 'UNKNOWN';
}

function monotonicTime(): number | null {
  return typeof performance === 'undefined' ? null : performance.now();
}

function warnUnavailable(message: string, error?: unknown): void {
  if (warnedUnavailable) {
    return;
  }
  warnedUnavailable = true;
  console.warn(`[HBox WebHID] Network trace ${message}`, error);
}
