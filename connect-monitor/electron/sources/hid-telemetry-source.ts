import { rxProfileDecoder } from "./rx-profile";
import { buildFastControl } from "./fast-recovery-control";
import type { FastRequest } from "../../shared/fast-recovery";
import { RelativeLatencyDecoder } from "./relative-latency";
import type { DebugConfig, DebugConfigStatus, DebugApplyState, MonitorEvent } from "../../shared/monitor-types";
import { buttonLatencyTracker, monotonicNowUsForMonitor } from "./button-latency-source";
import { parseApplicationHidTelemetryFrame } from "./application-hid-telemetry-source";
import { parseDongleHidTelemetryFrame } from "./dongle-hid-telemetry-source";
import { matchesHidTelemetryDevice } from "./hid-device-selection";

type PublishFn = (event: MonitorEvent) => void;
type SourceOptions = {
  onControlReady?: () => void;
};

const CTL_MAGIC = 0x314c5443;
const CTL_VERSION = 1;
const CTL_FRAME_SIZE = 32;
const CMD_HID_TELEMETRY_LEASE = 5;
const CAP_HID_TELEMETRY_LEASE = 0x01;
const FLAG_HID_TELEMETRY = 0x01;
const FLAG_AUTO_HOP = 0x10;
const APPLY_STATES: DebugApplyState[] = ["Idle", "Applied", "Applying", "Failed"];

let activeControlHandles: any[] = [];
const telemetryLeaseSupport = new Map<any, boolean>();
let preferredControlHandle: any | null = null;
let nextControlSeq = 1;
let fastLeaseActive = false;
let fastLeaseId = 1;
let currentHidTelemetryEnabled = false;
let currentLatencyEnabled = false;
let desiredConfig: DebugConfig | null = null;
let requestedControlSeq = 0;
let lastControlAttemptAt = 0;
let controlWriteSucceeded = false;
let sourceGeneration = 0;
let controlPending = 0;
let controlTail: Promise<void> = Promise.resolve();

// Serialize control transfers only. Awaiting native I/O leaves the worker's
// data callbacks, MessagePort and forwarding timers free to run.
function queueControl<T>(action: () => Promise<T>): Promise<T> {
  if (controlPending >= 16) return Promise.reject(new Error("HID control queue full"));
  const generation = sourceGeneration;
  controlPending++;
  const result = controlTail.then(() => {
    if (generation !== sourceGeneration) throw new Error("HID control session expired");
    return action();
  });
  controlTail = result.then(() => {}, () => {}).finally(() => { controlPending--; });
  return result;
}

function liveHandle(handle: any): boolean { return activeControlHandles.includes(handle); }
const CONTROL_RETRY_INTERVAL_MS = 2000;
const relativeLatency=new RelativeLatencyDecoder();
let debugStatus: DebugConfigStatus = {
  state: "Idle",
  rxStatus: "Idle",
  txStatus: "Idle",
  lastSeq: 0,
};

function normalizeHexId(value: string | number | undefined): number | null {
  if (value === undefined) return null;
  if (typeof value === "number") return value;
  const n = Number(value);
  return Number.isFinite(n) ? n : null;
}

function publishRfDeviceMissing(publish: PublishFn): void {
  publish({
    kind: "device_status",
    timestampMs: Date.now(),
    mode: "RF24G",
    state: "Disconnected",
    statusLabel: "设备未接入",
    targetRateHz: 0,
    actualRateHz: 0,
  });
}

function putU16LE(buf: Buffer, offset: number, value: number): void {
  buf[offset] = value & 0xff;
  buf[offset + 1] = (value >> 8) & 0xff;
}

function putU32LE(buf: Buffer, offset: number, value: number): void {
  buf[offset] = value & 0xff;
  buf[offset + 1] = (value >> 8) & 0xff;
  buf[offset + 2] = (value >> 16) & 0xff;
  buf[offset + 3] = (value >> 24) & 0xff;
}

function getU16LE(data: Uint8Array, offset: number): number {
  return data[offset] | (data[offset + 1] << 8);
}

function getU32LE(data: Uint8Array, offset: number): number {
  return data[offset] | (data[offset + 1] << 8) | (data[offset + 2] << 16) | (data[offset + 3] << 24);
}

function crc16Ccitt(data: Uint8Array, len: number): number {
  let crc = 0xffff;
  for (let i = 0; i < len; i++) {
    crc ^= data[i] << 8;
    for (let bit = 0; bit < 8; bit++) {
      crc = (crc & 0x8000) !== 0 ? ((crc << 1) ^ 0x1021) & 0xffff : (crc << 1) & 0xffff;
    }
  }
  return crc & 0xffff;
}

function configFlags(config: DebugConfig): number {
  return (config.hidTelemetryEnabled ? FLAG_HID_TELEMETRY : 0) |
    (config.autoHopEnabled ? FLAG_AUTO_HOP : 0) |
    (config.latencyMeasurementEnabled && config.hidTelemetryEnabled ? 0x20 : 0);
}

function buildControlFrame(config: DebugConfig, seq: number, target = 0): Buffer {
  const frame = Buffer.alloc(CTL_FRAME_SIZE);
  putU32LE(frame, 0, CTL_MAGIC);
  frame[4] = CTL_VERSION;
  frame[5] = seq & 0xff;
  frame[6] = target;
  frame[7] = 1;
  putU32LE(frame, 8, configFlags(config));
  putU16LE(frame, 12, config.hidTelemetryEnabled ? config.hidPeriodMs : 0);
  putU16LE(frame, 14, crc16Ccitt(frame, 14));
  frame[16] = typeof config.manualChannel === "number" ? config.manualChannel : 0xff;
  return frame;
}

function buildCaptureLeaseFrame(): Buffer {
  const frame=Buffer.alloc(CTL_FRAME_SIZE);putU32LE(frame,0,CTL_MAGIC);
  frame[4]=CTL_VERSION;frame[7]=4;putU16LE(frame,14,crc16Ccitt(frame,14));return frame;
}

function buildTelemetryLeaseFrame(enabled: boolean): Buffer {
  const frame = Buffer.alloc(CTL_FRAME_SIZE);
  putU32LE(frame, 0, CTL_MAGIC);
  frame[4] = CTL_VERSION;
  frame[6] = 1; // RX-local USB control; never forwarded over RF.
  frame[7] = CMD_HID_TELEMETRY_LEASE;
  putU32LE(frame, 8, enabled ? FLAG_HID_TELEMETRY : 0);
  putU16LE(frame, 14, crc16Ccitt(frame, 14));
  return frame;
}

function statusFromCode(code: number): DebugApplyState {
  return APPLY_STATES[code] ?? "Failed";
}

function combineStatus(rxStatus: DebugApplyState, txStatus: DebugApplyState): DebugApplyState {
  const states = [rxStatus, txStatus];
  if (states.some((state) => state === "Failed")) return "Failed";
  if (states.some((state) => state === "Applying")) return "Partial";
  if (states.every((state) => state === "Applied" || state === "Idle")) return "Applied";
  return "Applying";
}

type DeviceConfigStatus = DebugConfigStatus & { flags: number; txAppliedSeq: number };

function parseStatusReport(raw: Uint8Array): DeviceConfigStatus | null {
  const data = raw.length >= CTL_FRAME_SIZE + 1 && getU32LE(raw, 1) === CTL_MAGIC ? raw.subarray(1) : raw;
  if (data.length < CTL_FRAME_SIZE) return null;
  if (getU32LE(data, 0) !== CTL_MAGIC || data[4] !== CTL_VERSION) return null;
  const crc = getU16LE(data, 18);
  if (crc16Ccitt(data, 18) !== crc) return null;

  const rxStatus = statusFromCode(data[6]);
  const txStatus = statusFromCode(data[7]);
  return {
    state: combineStatus(rxStatus, txStatus),
    rxStatus,
    txStatus,
    lastSeq: data[5],
    flags: getU32LE(data, 8),
    txAppliedSeq: data[15],
    telemetryLeaseSupported: (data[16] & CAP_HID_TELEMETRY_LEASE) !== 0,
  };
}

async function refreshDebugStatus(handle: any): Promise<DeviceConfigStatus | null> {
  if (!liveHandle(handle) || typeof handle.getFeatureReport !== "function") return null;
  try {
    const report = await handle.getFeatureReport(0, CTL_FRAME_SIZE + 1);
    if (!liveHandle(handle)) return null;
    const parsed = parseStatusReport(Uint8Array.from(report));
    if (parsed) {
      telemetryLeaseSupport.set(handle, parsed.telemetryLeaseSupported === true);
      // GET_REPORT can still describe the previous SET_REPORT. A successful
      // USB write is not proof that this RX/TX configuration was applied.
      if (parsed.lastSeq === requestedControlSeq) {
        if (desiredConfig?.hidTelemetryEnabled && !parsed.telemetryLeaseSupported) {
          debugStatus = {
            state: "Failed", rxStatus: "Failed", txStatus: "Failed",
            lastSeq: requestedControlSeq, telemetryLeaseSupported: false,
            message: "RX 固件不支持监视器统计租约；请更新 RX 固件",
          };
          return parsed;
        }
        const matches = desiredConfig && parsed.flags === configFlags(desiredConfig);
        debugStatus = {
          state: matches ? parsed.state : "Partial",
          rxStatus: matches ? parsed.rxStatus : "Applying",
          txStatus: parsed.txStatus === "Applied" && parsed.txAppliedSeq !== requestedControlSeq
            ? "Applying" : parsed.txStatus,
          lastSeq: requestedControlSeq,
          telemetryLeaseSupported: parsed.telemetryLeaseSupported,
        };
        debugStatus.state = combineStatus(debugStatus.rxStatus, debugStatus.txStatus);
      }
      return parsed;
    }
  } catch (_err) {
    // Some HID backends do not support feature GET_REPORT on this interface.
  }
  return null;
}

async function writeControlFrame(handle: any, frame: Buffer): Promise<boolean> {
  if (!liveHandle(handle)) return false;
  try {
    if (typeof handle.sendFeatureReport === "function") {
      await handle.sendFeatureReport([0, ...frame]);
      return liveHandle(handle);
    }
  } catch (_err) {
    // Fall through to interrupt OUT/control fallback below.
  }

  try {
    if (liveHandle(handle) && typeof handle.write === "function") {
      await handle.write([0, ...frame]);
      return liveHandle(handle);
    }
  } catch (_err) {
    return false;
  }
  return false;
}

export function sendFastRecovery(request: FastRequest, deadline = Date.now() + 2000): Promise<{ok:boolean;message?:string}> {
  const frame=buildFastControl(request);
  return queueControl(async () => {
  if (Date.now() > deadline) return {ok:false,message:"Control request expired before execution"};
  const handle=preferredControlHandle??activeControlHandles[0];
  if(!handle || !await writeControlFrame(handle,frame))return {ok:false,message:"No writable RX HID interface"};
  if(request.operation===1)fastLeaseActive=!!request.enabled;
  if(request.operation===2)fastLeaseActive=true;
  fastLeaseId=request.testId;
  return {ok:true,message:"USB write accepted; wait for firmware status"};
  });
}
export function getHidDebugConfigStatus(): DebugConfigStatus {
  return debugStatus;
}

export function sendDebugConfig(config: DebugConfig): Promise<DebugConfigStatus> {
  return queueControl(() => applyDebugConfig(config));
}

async function applyDebugConfig(config: DebugConfig): Promise<DebugConfigStatus> {
  const generation = sourceGeneration;
  currentHidTelemetryEnabled = Boolean(config.hidTelemetryEnabled);
  currentLatencyEnabled = currentHidTelemetryEnabled && config.latencyMeasurementEnabled === true;
  if (!config.autoHopEnabled && typeof config.manualChannel !== "number") {
    desiredConfig = null;
    debugStatus = {
      state: "Failed",
      rxStatus: "Failed",
      txStatus: "Failed",
      lastSeq: nextControlSeq,
      message: "Manual channel required when auto hop is disabled",
    };
    return debugStatus;
  }

  desiredConfig = { ...config };
  lastControlAttemptAt = Date.now();
  controlWriteSucceeded = false;

  const seq = nextControlSeq;
  requestedControlSeq = seq;
  nextControlSeq = nextControlSeq === 255 ? 1 : nextControlSeq + 1;
  const frame = buildControlFrame(config, seq);
  const handles = preferredControlHandle
    ? [preferredControlHandle, ...activeControlHandles.filter((handle) => handle !== preferredControlHandle)]
    : [...activeControlHandles];

  debugStatus = {
    state: "Applying",
    rxStatus: "Applying",
    txStatus: "Applying",
    lastSeq: seq,
  };

  if (handles.length === 0) {
    debugStatus = {
      ...debugStatus,
      state: "Failed",
      message: "No HID control device",
    };
    return debugStatus;
  }

  for (const handle of handles) {
    if (config.hidTelemetryEnabled) {
      const capability = await refreshDebugStatus(handle);
      if (!capability?.telemetryLeaseSupported) continue;
    }
    if (!await writeControlFrame(handle, frame)) {
      if (generation !== sourceGeneration) return debugStatus;
      continue;
    }
    preferredControlHandle = handle;
    controlWriteSucceeded = true;
    await refreshDebugStatus(handle);
    return debugStatus;
  }

  debugStatus = {
    ...debugStatus,
    state: "Failed",
    rxStatus: "Failed",
    message: config.hidTelemetryEnabled && handles.every((handle) => telemetryLeaseSupport.get(handle) !== true)
      ? "RX 固件不支持监视器统计租约，或无法读取能力；请更新 RX 固件"
      : "HID SET_REPORT failed on all interfaces",
  };
  return debugStatus;
}

const DEVICE_RESCAN_INTERVAL_MS = 1000;
const MISSING_STATUS_INTERVAL_MS = 3000;

export function startHidTelemetrySource(publish: PublishFn, options: SourceOptions = {}): () => Promise<void> {
  sourceGeneration++;
  let HID: any;
  try {
    HID = require("node-hid");
    if (!HID.HIDAsync || !HID.devicesAsync) throw new Error("node-hid asynchronous API required");
  } catch (_err) {
    throw new Error(`Cannot start asynchronous HID reader: ${String(_err)}`);
  }

  const targetVid = normalizeHexId(process.env.MONITOR_VID);
  const targetPid = normalizeHexId(process.env.MONITOR_PID) ?? null;
  const opened: any[] = [];
  let stopped = false;
  let lastMissingStatusAt = 0;
  let rfWasConnected = false;
  let lastRfTelemetryAt = 0;
  let lastStatusCheckAt = 0;
  let scanning: Promise<void> | null = null;
  const closingHandles = new Map<any, Promise<void>>();

  const publishMissingThrottled = () => {
    const now = Date.now();
    if (now - lastMissingStatusAt < MISSING_STATUS_INTERVAL_MS) return;
    lastMissingStatusAt = now;
    publishRfDeviceMissing(publish);
  };

  const findTargetDevices = async () => {
    return (await HID.devicesAsync()).filter((device: any) =>
      matchesHidTelemetryDevice(device, {
        vendorId: targetVid,
        productId: targetPid,
      }),
    );
  };

  const closeHandle = (handle: any): Promise<void> => {
    const closing = closingHandles.get(handle);
    if (closing) return closing;
    sourceGeneration++;
    fastLeaseActive=false;
    rfWasConnected = false;
    controlWriteSucceeded = false;
    debugStatus = { ...debugStatus, state: "Failed", rxStatus: "Failed", txStatus: "Failed", message: "HID disconnected" };
    buttonLatencyTracker.reset();
    const idx = opened.indexOf(handle);
    if (idx >= 0) {
      opened.splice(idx, 1);
    }
    activeControlHandles = activeControlHandles.filter((h) => h !== handle);
    telemetryLeaseSupport.delete(handle);
    if (preferredControlHandle === handle) {
      preferredControlHandle = null;
    }
    handle.removeAllListeners("data");
    const done = Promise.resolve().then(() => handle.close()).catch(() => {}).finally(() => closingHandles.delete(handle));
    closingHandles.set(handle, done);
    return done;
  };

  const openDevices = async () => {
    if (stopped || opened.length > 0 || closingHandles.size) return;

    const devices = await findTargetDevices();
    if (stopped) return;
    if (devices.length === 0) {
      publishMissingThrottled();
      return;
    }

    for (const dev of devices) {
      if (stopped) break;
      try {
        const handle = dev.path ? await HID.HIDAsync.open(dev.path) : await HID.HIDAsync.open(dev.vendorId, dev.productId);
        if (stopped) { await handle.close(); break; }
        handle.on("data", (buf: Uint8Array) => {
          if (stopped || !liveHandle(handle)) return;
          try {
            const relative=relativeLatency.parse(buf);
            if(relative!==null){for(const ev of relative)publish(ev);return;}
            const hostMonoUs = monotonicNowUsForMonitor();
            const appEvents = parseApplicationHidTelemetryFrame(buf);
            if (appEvents.length > 0) {
              for (const ev of appEvents) publish(ev);
              return;
            }
            const dongleEvents = parseDongleHidTelemetryFrame(buf, Date.now(), hostMonoUs);
            for (const ev of dongleEvents) {
              if (ev.kind === "packet" && ev.messageType.startsWith("RFH_RHM1_"))
                lastRfTelemetryAt = ev.timestampMs;
              // A TX-only restart leaves the RX USB handle and its previous
              // "Applied" status alive. Reapply the current configuration once
              // after RF recovery; a capture lease alone cannot enable TX.
              if (ev.kind === "device_status") {
                const connected = ev.state === "Connected";
                const recovered = connected && !rfWasConnected;
                rfWasConnected = connected;
                if (recovered) options.onControlReady?.();
              }
              if (ev.kind === "device_status" && ["Disconnected", "Reconnecting", "Error"].includes(ev.state))
                buttonLatencyTracker.reset();
              if (ev.kind === "packet" && (ev.messageType === "RFH_RHL1" || ev.messageType === "RFH_RHL2")) {
                buttonLatencyTracker.handleLatencyPacket(ev, publish);
              }
              if (ev.kind === "packet" && (ev.messageType === "RFH_RHC3" || ev.messageType === "RFH_RHC4" || ev.messageType === "RFH_RHE3"))
                buttonLatencyTracker.handleTracePacket(ev,publish);
              publish(ev);
            }
          } catch (_err) {
            publishRfDeviceMissing(publish);
          }
        });
        handle.on("error", () => {
          void closeHandle(handle);
          if (!stopped) publishRfDeviceMissing(publish);
        });
        opened.push(handle);
        activeControlHandles.push(handle);
        buttonLatencyTracker.reset();relativeLatency.reset();rxProfileDecoder.reset();
        options.onControlReady?.();
      } catch (_err) {
        publishMissingThrottled();
        // ignore a single device open failure to keep monitor running
      }
    }

    if (!stopped && opened.length === 0) {
      publishMissingThrottled();
    }
  };

  const scanAndOpen = () => {
    if (scanning || stopped) return;
    scanning = openDevices().catch(() => { if (!stopped) publishMissingThrottled(); }).finally(() => { scanning = null; });
  };
  scanAndOpen();
  const rescanTimer = setInterval(scanAndOpen, DEVICE_RESCAN_INTERVAL_MS);
  let maintenancePending = false;
  const captureLeaseTimer=setInterval(()=>{
    if (stopped || maintenancePending || controlPending) return;
    maintenancePending = true;
    void queueControl(async () => {
    if(currentHidTelemetryEnabled && preferredControlHandle && telemetryLeaseSupport.get(preferredControlHandle))
      await writeControlFrame(preferredControlHandle, buildTelemetryLeaseFrame(true));
    if(fastLeaseActive && preferredControlHandle)await writeControlFrame(preferredControlHandle,buildFastControl({operation:5,testId:fastLeaseId}));
    if(currentLatencyEnabled && preferredControlHandle)await writeControlFrame(preferredControlHandle,buildCaptureLeaseFrame());
    if (!desiredConfig) return;
    const handle = preferredControlHandle ?? activeControlHandles[0];
    if (!handle) return;
    const now = Date.now();
    const telemetrySilent = desiredConfig.hidTelemetryEnabled &&
      now - Math.max(lastRfTelemetryAt, lastControlAttemptAt) > 3000;
    // Once configuration is applied and RHM1 is flowing, polling GET_REPORT
    // adds a blocking control transfer without changing any decision. Keep
    // the one-second USB lease, but read status only while applying or stale.
    if (debugStatus.state === "Applied" && !telemetrySilent) return;
    if (now - lastStatusCheckAt < CONTROL_RETRY_INTERVAL_MS) return;
    lastStatusCheckAt = now;
    const status = await refreshDebugStatus(handle);
    if (stopped || !liveHandle(handle)) return;
    // Re-enable an expired RX-only telemetry lease without resending the
    // unchanged TX configuration over RF.
    const telemetryOnlyExpired = desiredConfig.hidTelemetryEnabled && status?.telemetryLeaseSupported &&
      status.lastSeq === requestedControlSeq && status.txAppliedSeq === requestedControlSeq &&
      status.txStatus === "Applied" &&
      (status.flags & FLAG_HID_TELEMETRY) === 0 &&
      (status.flags & ~FLAG_HID_TELEMETRY) === (configFlags(desiredConfig) & ~FLAG_HID_TELEMETRY);
    if (telemetryOnlyExpired) {
      if (await writeControlFrame(handle, buildControlFrame(desiredConfig, requestedControlSeq, 1)))
        await refreshDebugStatus(handle);
      return;
    }
    // Keepalive (cmd 4) only renews an already enabled capture on RX. It
    // cannot recover a missed enable, an expired lease, or a TX restart.
    // Reconcile over USB; only a mismatch causes another RF configuration.
    const needsApply = !controlWriteSucceeded || !preferredControlHandle || (status !== null && (
      status.lastSeq !== requestedControlSeq ||
      status.flags !== configFlags(desiredConfig) ||
      status.rxStatus !== "Applied" ||
      (rfWasConnected && (status.txStatus !== "Applied" || status.txAppliedSeq !== requestedControlSeq))
    ));
    if (needsApply && (!desiredConfig.hidTelemetryEnabled || telemetryLeaseSupport.get(handle) !== false) &&
        Date.now() - lastControlAttemptAt >= CONTROL_RETRY_INTERVAL_MS)
      await applyDebugConfig(desiredConfig);
    }).catch(() => {}).finally(() => { maintenancePending = false; });
  },1000);

  return async () => {
    stopped = true;
    clearInterval(rescanTimer);clearInterval(captureLeaseTimer);
    // A normal pause/quit releases the USB-only capture before closing HID.
    // A stalled or crashed worker is covered by the firmware lease timeout.
    await controlTail;
    const handle = preferredControlHandle ?? activeControlHandles[0];
    if (handle && telemetryLeaseSupport.get(handle))
      await writeControlFrame(handle, buildTelemetryLeaseFrame(false));
    sourceGeneration++;
    relativeLatency.reset();rxProfileDecoder.reset();
    buttonLatencyTracker.reset();
    for (const h of [...opened]) {
      void closeHandle(h);
    }
    activeControlHandles = [];
    preferredControlHandle = null;
    await Promise.allSettled([scanning, ...closingHandles.values(), controlTail]);
  };
}
