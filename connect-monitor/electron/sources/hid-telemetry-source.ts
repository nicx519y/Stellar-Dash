import type { DebugConfig, DebugConfigStatus, DebugApplyState, MonitorEvent } from "../../shared/monitor-types";
import { buttonLatencyTracker, monotonicNowUsForMonitor } from "./button-latency-source";
import { parseApplicationHidTelemetryFrame } from "./application-hid-telemetry-source";
import { parseDongleHidTelemetryFrame } from "./dongle-hid-telemetry-source";

type PublishFn = (event: MonitorEvent) => void;
type SourceOptions = {
  onControlReady?: () => void;
};

const CTL_MAGIC = 0x314c5443;
const CTL_VERSION = 1;
const CTL_FRAME_SIZE = 32;
const FLAG_HID_TELEMETRY = 0x01;
const FLAG_AUTO_HOP = 0x10;
const APPLY_STATES: DebugApplyState[] = ["Idle", "Applied", "Applying", "Failed"];

let activeControlHandles: any[] = [];
let preferredControlHandle: any | null = null;
let nextControlSeq = 1;
let nextTimeSyncSeq = 1;
let currentHidTelemetryEnabled = false;
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
    (config.autoHopEnabled ? FLAG_AUTO_HOP : 0);
}

function buildControlFrame(config: DebugConfig, seq: number): Buffer {
  const frame = Buffer.alloc(CTL_FRAME_SIZE);
  putU32LE(frame, 0, CTL_MAGIC);
  frame[4] = CTL_VERSION;
  frame[5] = seq & 0xff;
  frame[6] = 0;
  frame[7] = 1;
  putU32LE(frame, 8, configFlags(config));
  putU16LE(frame, 12, config.hidTelemetryEnabled ? config.hidPeriodMs : 0);
  putU16LE(frame, 14, crc16Ccitt(frame, 14));
  frame[16] = typeof config.manualChannel === "number" ? config.manualChannel : 0xff;
  return frame;
}

function buildTimeSyncFrame(seq: number): Buffer {
  const frame = Buffer.alloc(CTL_FRAME_SIZE);
  putU32LE(frame, 0, CTL_MAGIC);
  frame[4] = CTL_VERSION;
  frame[5] = seq & 0xff;
  frame[6] = 0;
  frame[7] = 3;
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

function parseStatusReport(raw: Uint8Array): DebugConfigStatus | null {
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
  };
}

function refreshDebugStatus(handle: any): void {
  if (!handle || typeof handle.getFeatureReport !== "function") return;
  try {
    const report = handle.getFeatureReport(0, CTL_FRAME_SIZE + 1);
    const parsed = parseStatusReport(Uint8Array.from(report));
    if (parsed) {
      debugStatus = parsed;
    }
  } catch (_err) {
    // Some HID backends do not support feature GET_REPORT on this interface.
  }
}

function writeControlFrame(handle: any, frame: Buffer): boolean {
  try {
    if (typeof handle.sendFeatureReport === "function") {
      handle.sendFeatureReport([0, ...frame]);
      return true;
    }
  } catch (_err) {
    // Fall through to interrupt OUT/control fallback below.
  }

  try {
    if (typeof handle.write === "function") {
      handle.write([0, ...frame]);
      return true;
    }
  } catch (_err) {
    return false;
  }
  return false;
}

export function getHidDebugConfigStatus(): DebugConfigStatus {
  return debugStatus;
}

export function sendDebugConfig(config: DebugConfig): DebugConfigStatus {
  currentHidTelemetryEnabled = Boolean(config.hidTelemetryEnabled);
  if (!config.autoHopEnabled && typeof config.manualChannel !== "number") {
    debugStatus = {
      state: "Failed",
      rxStatus: "Failed",
      txStatus: "Failed",
      lastSeq: nextControlSeq,
      message: "Manual channel required when auto hop is disabled",
    };
    return debugStatus;
  }

  const seq = nextControlSeq;
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
    if (!writeControlFrame(handle, frame)) {
      continue;
    }
    preferredControlHandle = handle;
    refreshDebugStatus(handle);
    return debugStatus;
  }

  debugStatus = {
    ...debugStatus,
    state: "Failed",
    message: "HID SET_REPORT failed on all interfaces",
  };
  return debugStatus;
}

function sendTimeSync(handle: any): void {
  if (!currentHidTelemetryEnabled || !handle) return;
  const seq = nextTimeSyncSeq;
  nextTimeSyncSeq = nextTimeSyncSeq === 255 ? 1 : nextTimeSyncSeq + 1;
  const frame = buildTimeSyncFrame(seq);
  const pcT0Us = monotonicNowUsForMonitor();
  if (writeControlFrame(handle, frame)) {
    buttonLatencyTracker.noteTimeSyncSent(seq, pcT0Us);
  }
}

const defaultTargetUsbIds = [
  { vendorId: 0x045e, productId: 0x028e },
  { vendorId: 0x045e, productId: 0x02ff },
  { vendorId: 0x1a86, productId: 0xfe0c },
];
const DEVICE_RESCAN_INTERVAL_MS = 1000;
const MISSING_STATUS_INTERVAL_MS = 3000;

function textIncludes(value: unknown, needle: string): boolean {
  return typeof value === "string" && value.toLowerCase().includes(needle);
}

function matchesDefaultUsbId(device: any): boolean {
  return defaultTargetUsbIds.some(
    (id) => device.vendorId === id.vendorId && device.productId === id.productId,
  );
}

function isLikelyHBoxDevice(device: any): boolean {
  return textIncludes(device.manufacturer, "hbox") || textIncludes(device.product, "hbox");
}

function isGenericDesktopController(device: any): boolean {
  return device.usagePage === 0x01 && (device.usage === 0x04 || device.usage === 0x05);
}

function isLikelyTelemetryInterface(device: any): boolean {
  const interfaceNumber =
    typeof device.interface === "number"
      ? device.interface
      : typeof device.interfaceNumber === "number"
        ? device.interfaceNumber
        : undefined;

  if (device.usagePage === 0xff00) return true;
  if (interfaceNumber === 3 && isLikelyHBoxDevice(device)) return true;
  return false;
}

export function startHidTelemetrySource(publish: PublishFn, options: SourceOptions = {}): () => void {
  let HID: any;
  try {
    HID = require("node-hid");
  } catch (_err) {
    return () => {};
  }

  const targetVid = normalizeHexId(process.env.MONITOR_VID);
  const targetPid = normalizeHexId(process.env.MONITOR_PID) ?? null;
  const hasExplicitUsbTarget = targetVid !== null || targetPid !== null;
  const opened: any[] = [];
  let stopped = false;
  let lastMissingStatusAt = 0;
  let lastTimeSyncAt = 0;

  const publishMissingThrottled = () => {
    const now = Date.now();
    if (now - lastMissingStatusAt < MISSING_STATUS_INTERVAL_MS) return;
    lastMissingStatusAt = now;
    publishRfDeviceMissing(publish);
  };

  const findTargetDevices = () => {
    return HID.devices().filter((d: any) => {
      if (hasExplicitUsbTarget) {
        if (targetVid !== null && d.vendorId !== targetVid) return false;
        if (targetPid !== null && d.productId !== targetPid) return false;
        return !isGenericDesktopController(d);
      }

      if (matchesDefaultUsbId(d)) return isLikelyTelemetryInterface(d);
      if (!isLikelyHBoxDevice(d)) return false;
      if (targetPid !== null && d.productId !== targetPid) return false;
      return isLikelyTelemetryInterface(d);
    });
  };

  const closeHandle = (handle: any) => {
    const idx = opened.indexOf(handle);
    if (idx >= 0) {
      opened.splice(idx, 1);
    }
    activeControlHandles = activeControlHandles.filter((h) => h !== handle);
    if (preferredControlHandle === handle) {
      preferredControlHandle = null;
    }
    try {
      handle.close();
    } catch (_err) {
      // ignore
    }
  };

  const scanAndOpen = () => {
    if (stopped || opened.length > 0) return;

    const devices = findTargetDevices();
    if (devices.length === 0) {
      publishMissingThrottled();
      return;
    }

    for (const dev of devices) {
      try {
        const handle = dev.path ? new HID.HID(dev.path) : new HID.HID(dev.vendorId, dev.productId);
        handle.on("data", (buf: Uint8Array) => {
          try {
            const hostMonoUs = monotonicNowUsForMonitor();
            const appEvents = parseApplicationHidTelemetryFrame(buf);
            if (appEvents.length > 0) {
              for (const ev of appEvents) publish(ev);
              return;
            }
            const dongleEvents = parseDongleHidTelemetryFrame(buf, Date.now(), hostMonoUs);
            for (const ev of dongleEvents) {
              if (ev.kind === "packet" && (ev.messageType === "RFH_RHL1" || ev.messageType === "RFH_RHL2")) {
                buttonLatencyTracker.handleLatencyPacket(ev, publish);
              }
              publish(ev);
            }
          } catch (_err) {
            publishRfDeviceMissing(publish);
          }
        });
        handle.on("error", () => {
          closeHandle(handle);
          publishRfDeviceMissing(publish);
        });
        opened.push(handle);
        activeControlHandles.push(handle);
        buttonLatencyTracker.reset();
        options.onControlReady?.();
      } catch (_err) {
        publishMissingThrottled();
        // ignore a single device open failure to keep monitor running
      }
    }

    if (opened.length === 0) {
      publishMissingThrottled();
    }
  };

  scanAndOpen();
  const rescanTimer = setInterval(scanAndOpen, DEVICE_RESCAN_INTERVAL_MS);
  const timeSyncTimer = setInterval(() => {
    if (!currentHidTelemetryEnabled) {
      buttonLatencyTracker.publishStatus("No HID telemetry", publish);
      return;
    }
    buttonLatencyTracker.publishStatus("Waiting edge", publish);
  }, 50);

  return () => {
    stopped = true;
    clearInterval(rescanTimer);
    clearInterval(timeSyncTimer);
    for (const h of [...opened]) {
      closeHandle(h);
    }
    activeControlHandles = [];
    preferredControlHandle = null;
  };
}
