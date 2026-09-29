import { MonitorStreamProcessor } from "./monitorStreamProcessor";
import type { MonitorStreamSnapshot, MonitorStreamWorkerRequest, MonitorStreamWorkerResponse } from "./monitorStreamTypes";

const processor = new MonitorStreamProcessor();
const workerScope = self as unknown as {
  postMessage: (message: MonitorStreamWorkerResponse) => void;
  onmessage: ((event: MessageEvent<MonitorStreamWorkerRequest>) => void) | null;
};

let flushTimer: ReturnType<typeof setTimeout> | null = null;
let snapshotInFlight = false;
let dirty = false;
let previous: MonitorStreamSnapshot | null = null;

function sameSection(a: unknown, b: unknown): boolean {
  if (a === b) return true;
  if (!a || !b || typeof a !== "object" || typeof b !== "object" || Array.isArray(a) || Array.isArray(b)) return false;
  const left = a as Record<string, unknown>, right = b as Record<string, unknown>;
  const keys = Object.keys(left);
  return keys.length === Object.keys(right).length && keys.every(key => left[key] === right[key]);
}

function postSnapshot(): void {
  if (flushTimer !== null) {
    clearTimeout(flushTimer);
    flushTimer = null;
  }
  dirty = true;
  if (snapshotInFlight) return;
  dirty = false;
  snapshotInFlight = true;
  const snapshot = processor.snapshot();
  // Processor arrays are immutable; unchanged sections can be recognized in
  // constant time, before MessagePort cloning destroys their identity.
  const patch = Object.fromEntries(Object.entries(snapshot).filter(([key, value]) =>
    !previous || !sameSection(value, previous[key as keyof MonitorStreamSnapshot]),
  )) as Partial<MonitorStreamSnapshot>;
  previous = snapshot;
  workerScope.postMessage({
    type: "snapshot",
    snapshot: patch,
  });
}

function scheduleSnapshot(): void {
  dirty = true;
  if (flushTimer !== null || snapshotInFlight) return;
  flushTimer = setTimeout(postSnapshot, 33);
}

workerScope.onmessage = (event) => {
  const message = event.data;
  if (message.type === "snapshotConsumed") {
    snapshotInFlight = false;
    if (dirty) scheduleSnapshot();
    return;
  }
  if (message.type === "batch") {
    processor.processBatch(message.events);
    scheduleSnapshot();
    if (message.requestId !== undefined) {
      workerScope.postMessage({ type: "processed", requestId: message.requestId });
    }
    return;
  }
  if (message.type === "prependEvents") {
    processor.prependEvents(message.events);
    postSnapshot();
    return;
  }
  if (message.type === "reset") {
    processor.clear();
    postSnapshot();
    return;
  }
  if (message.type === "flush") {
    postSnapshot();
  }
};

postSnapshot();
