// All native control I/O is asynchronous, including inside this reader worker.
import { parentPort } from "node:worker_threads";
import { BoundedDelivery } from "../pipeline/bounded-delivery";
import { getHidDebugConfigStatus, sendDebugConfig, sendFastRecovery, startHidTelemetrySource } from "./hid-telemetry-source";
import type { DebugConfig, MonitorEvent } from "../../shared/monitor-types";
import type { FastRequest } from "../../shared/fast-recovery";

const port = parentPort!;
const events = new BoundedDelivery<MonitorEvent>(4000, 500);
let configRevision = 0;
let stopped = false;
let configError: string | undefined;
const status = () => port.postMessage({ type: "status", revision: configRevision,
  status: configError ? { ...getHidDebugConfigStatus(), state: "Failed", message: configError } : getHidDebugConfigStatus(), queue: events.stats() });
const stop = startHidTelemetrySource(event => events.enqueue([event]), {
  onControlReady: () => port.postMessage({ type: "ready" }),
});
const flush = () => events.flush((batch, sequence) => port.postMessage({ type: "events", batch, sequence }));
const timer = setInterval(flush, 20);
const statusTimer = setInterval(status, 500);
port.on("message", async (message: {
  type: string; sequence: number; revision: number; config: DebugConfig;
  id: number; deadline: number; request: FastRequest;
}) => {
  if (stopped) return;
  if (message.type === "ack") { events.acknowledge(message.sequence); flush(); }
  else if (message.type === "config") {
    try { await sendDebugConfig(message.config); configError = undefined; }
    catch (error) { configError = String(error); }
    if (stopped) return;
    configRevision = message.revision;
    status();
  } else if (message.type === "fast") {
    let result;
    try {
      result = Date.now() > message.deadline
        ? { ok: false, message: "Control request expired before execution" }
        : await sendFastRecovery(message.request, message.deadline);
    } catch (error) { result = { ok: false, message: String(error) }; }
    if (!stopped) port.postMessage({ type: "result", id: message.id, result });
  } else if (message.type === "stop") {
    stopped = true;
    clearInterval(timer);
    clearInterval(statusTimer);
    try { await stop(); } finally { port.close(); }
  }
});
