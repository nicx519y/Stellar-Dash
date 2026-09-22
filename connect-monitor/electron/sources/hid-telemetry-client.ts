import path from "node:path";
import { Worker } from "node:worker_threads";
import type { DebugConfig, DebugConfigStatus, MonitorEvent } from "../../shared/monitor-types";
import type { FastRequest } from "../../shared/fast-recovery";
import { buildFastControl } from "./fast-recovery-control";

type Result = { ok: boolean; message?: string };
type Session = {
  worker: Worker | null;
  stopped: boolean;
  configBusy: boolean;
  sentRevision: number;
  revision: number;
  pendingConfig: DebugConfig | null;
  pending: Map<number, { resolve: (result: Result) => void; timer: NodeJS.Timeout }>;
};
let session: Session | null = null;
let closing: Promise<unknown> = Promise.resolve();
let requestId = 0;
let queueStats: unknown = null;
let status: DebugConfigStatus = { state: "Idle", rxStatus: "Idle", txStatus: "Idle", lastSeq: 0 };

function fail(message: string): void {
  status = { ...status, state: "Failed", rxStatus: "Failed", txStatus: "Failed", message };
}

function dispatchConfig(current: Session): void {
  if (!current.worker || current.stopped || current.configBusy || !current.pendingConfig) return;
  current.configBusy = true;
  current.sentRevision = current.revision;
  current.worker.postMessage({ type: "config", revision: current.revision, config: current.pendingConfig });
  current.pendingConfig = null;
}

export function sendDebugConfig(config: DebugConfig): DebugConfigStatus {
  const current = session;
  if (!current || current.stopped) { fail("HID reader is not running"); return status; }
  // Coalesce rapid UI changes instead of accumulating stale device writes.
  current.pendingConfig = { ...config };
  current.revision++;
  status = { ...status, state: "Applying", rxStatus: "Applying", txStatus: "Applying", message: undefined };
  dispatchConfig(current);
  return status;
}

export function getHidDebugConfigStatus(): DebugConfigStatus { return status; }
export function getHidQueueStats(): unknown { return queueStats; }
export function waitForHidShutdown(): Promise<unknown> { return closing; }

export function sendFastRecovery(request: FastRequest): Promise<Result> {
  buildFastControl(request); // Validate before crossing the worker boundary.
  const current = session;
  if (!current?.worker || current.stopped) return Promise.resolve({ ok: false, message: "HID reader is not ready" });
  if (current.pending.size >= 8) return Promise.resolve({ ok: false, message: "HID control queue busy" });
  const id = ++requestId;
  return new Promise(resolve => {
    const timer = setTimeout(() => {
      // Keep the occupied slot until the worker replies. Otherwise repeated
      // timeouts could build an unbounded queue behind a blocked driver call.
      resolve({ ok: false, message: "HID reply timed out; device state is unconfirmed" });
    }, 3000);
    current.pending.set(id, { resolve, timer });
    current.worker!.postMessage({ type: "fast", id, request, deadline: Date.now() + 2000 });
  });
}

export function startHidTelemetrySource(publish: (event: MonitorEvent) => void, options: { onControlReady?: () => void } = {}): () => void {
  const current: Session = { worker: null, stopped: false, configBusy: false, sentRevision: -1, revision: 0, pendingConfig: null, pending: new Map() };
  session = current;
  status = { state: "Idle", rxStatus: "Idle", txStatus: "Idle", lastSeq: 0 };
  queueStats = null;
  const cancelRequests = () => {
    for (const item of current.pending.values()) {
      clearTimeout(item.timer);
      item.resolve({ ok: false, message: "HID reader stopped" });
    }
    current.pending.clear();
  };
  // A rapid pause/resume must not open a second reader before the old one closes.
  void closing.then(() => {
    if (current.stopped) return;
    const worker = new Worker(path.join(__dirname, "hid-telemetry-worker.js"));
    current.worker = worker;
    worker.on("message", message => {
      if (current.stopped || session !== current) return;
      if (message.type === "ready") options.onControlReady?.();
      else if (message.type === "events") {
        try { for (const event of message.batch as MonitorEvent[]) publish(event); }
        finally { worker.postMessage({ type: "ack", sequence: message.sequence }); }
      } else if (message.type === "status") {
        queueStats = message.queue;
        // Status for an older SET_REPORT must not overwrite a newer UI request.
        if (message.revision === current.revision) status = message.status;
        if (message.revision === current.sentRevision) current.configBusy = false;
        dispatchConfig(current);
      } else if (message.type === "result") {
        const item = current.pending.get(message.id);
        if (item) { clearTimeout(item.timer); current.pending.delete(message.id); item.resolve(message.result); }
      }
    });
    const failed = (message: string) => {
      if (current.stopped || session !== current) return;
      fail(message);
      publish({ kind: "device_status", timestampMs: Date.now(), mode: "RF24G", state: "Disconnected", statusLabel: message, targetRateHz: 0, actualRateHz: 0 });
      cancelRequests();
      current.stopped = true;
    };
    worker.on("error", error => failed(`HID worker: ${error.message}`));
    worker.on("exit", code => { current.worker = null; failed(`HID reader exited (${code}); pause/resume to retry`); });
    dispatchConfig(current);
  }).catch(error => {
    if (session === current && !current.stopped) { fail(String(error)); current.stopped = true; cancelRequests(); }
  });

  return () => {
    current.stopped = true;
    if (session === current) { session = null; fail("HID reader stopped"); }
    cancelRequests();
    const worker = current.worker;
    if (!worker) return;
    closing = new Promise<void>(resolve => {
      const timeout = setTimeout(() => { void worker.terminate(); }, 2000);
      worker.once("exit", () => { clearTimeout(timeout); resolve(); });
      worker.postMessage({ type: "stop" });
    });
  };
}
