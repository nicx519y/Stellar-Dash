import type { MonitorEvent } from "../pipeline/types";
import { parseApplicationHidTelemetryFrame } from "./application-hid-telemetry-source";
import { parseDongleHidTelemetryFrame } from "./dongle-hid-telemetry-source";

type PublishFn = (event: MonitorEvent) => void;

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

export function startHidTelemetrySource(publish: PublishFn): () => void {
  let HID: any;
  try {
    HID = require("node-hid");
  } catch (_err) {
    return () => {};
  }

  const targetVid = normalizeHexId(process.env.MONITOR_VID) ?? 0x045e;
  const targetPid = normalizeHexId(process.env.MONITOR_PID) ?? null;

  const devices = HID.devices().filter((d: any) => {
    if (d.vendorId !== targetVid) return false;
    if (targetPid !== null && d.productId !== targetPid) return false;
    if (d.usagePage === 0xff00) return true;
    if (typeof d.product === "string" && d.product.toLowerCase().includes("controller")) return true;
    return false;
  });

  if (devices.length === 0) {
    publishRfDeviceMissing(publish);
  }

  const opened: any[] = [];
  for (const dev of devices) {
    try {
      const handle = dev.path ? new HID.HID(dev.path) : new HID.HID(dev.vendorId, dev.productId);
      handle.on("data", (buf: Uint8Array) => {
        try {
          const appEvents = parseApplicationHidTelemetryFrame(buf);
          if (appEvents.length > 0) {
            for (const ev of appEvents) publish(ev);
            return;
          }
          const dongleEvents = parseDongleHidTelemetryFrame(buf);
          for (const ev of dongleEvents) publish(ev);
        } catch (_err) {
          publishRfDeviceMissing(publish);
        }
      });
      handle.on("error", () => {
        publishRfDeviceMissing(publish);
      });
      opened.push(handle);
    } catch (_err) {
      publishRfDeviceMissing(publish);
      // ignore a single device open failure to keep monitor running
    }
  }

  return () => {
    for (const h of opened) {
      try {
        h.close();
      } catch (_err) {
        // ignore
      }
    }
  };
}
