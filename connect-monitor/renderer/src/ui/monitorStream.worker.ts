import { MonitorStreamProcessor } from "./monitorStreamProcessor";
import type { MonitorStreamWorkerRequest, MonitorStreamWorkerResponse } from "./monitorStreamTypes";

const processor = new MonitorStreamProcessor();
const workerScope = self as unknown as {
  postMessage: (message: MonitorStreamWorkerResponse) => void;
  onmessage: ((event: MessageEvent<MonitorStreamWorkerRequest>) => void) | null;
};

let flushTimer: ReturnType<typeof setTimeout> | null = null;
let snapshotInFlight = false;
let dirty = false;

function postSnapshot(): void {
  if (flushTimer !== null) {
    clearTimeout(flushTimer);
    flushTimer = null;
  }
  dirty = true;
  if (snapshotInFlight) return;
  dirty = false;
  snapshotInFlight = true;
  workerScope.postMessage({
    type: "snapshot",
    snapshot: processor.snapshot(),
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
