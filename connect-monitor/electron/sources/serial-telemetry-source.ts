import type { MonitorEvent } from "../pipeline/types";
import { parseDongleTelemetryLine } from "./dongle-telemetry-source";

type PublishFn = (event: MonitorEvent) => void;

function parseUsbId(value: string | number | undefined): number | null {
  if (value === undefined || value === null) return null;
  if (typeof value === "number") return Number.isFinite(value) ? value : null;

  const text = String(value).trim();
  if (!text) return null;
  if (/^0x[0-9a-f]+$/i.test(text)) return parseInt(text.slice(2), 16);
  if (/^[0-9a-f]{4}$/i.test(text)) return parseInt(text, 16);

  const n = Number(text);
  return Number.isFinite(n) ? n : null;
}

function splitPaths(value: string | undefined): Set<string> {
  if (!value) return new Set();
  return new Set(
    value
      .split(/[;,]/)
      .map((p) => p.trim())
      .filter(Boolean),
  );
}

function portMatches(port: any, explicitPaths: Set<string>, targetVid: number, targetPid: number | null): boolean {
  if (explicitPaths.size > 0) return explicitPaths.has(port.path);

  const vid = parseUsbId(port.vendorId);
  const pid = parseUsbId(port.productId);
  if (vid === targetVid && (targetPid === null || pid === targetPid)) return true;

  const haystack = `${port.manufacturer ?? ""} ${port.friendlyName ?? ""} ${port.pnpId ?? ""}`.toLowerCase();
  return haystack.includes("hbox") && (targetPid === null || pid === targetPid);
}

export function startSerialTelemetrySource(publish: PublishFn): () => void {
  let SerialPortCtor: any;
  try {
    const serialport = require("serialport");
    SerialPortCtor = serialport.SerialPort ?? serialport;
  } catch (_err) {
    return () => {};
  }

  const explicitPaths = splitPaths(process.env.MONITOR_SERIAL_PATH);
  const targetVid =
    parseUsbId(process.env.MONITOR_SERIAL_VID) ??
    parseUsbId(process.env.MONITOR_VID) ??
    0x045e;
  const targetPid =
    parseUsbId(process.env.MONITOR_SERIAL_PID) ??
    parseUsbId(process.env.MONITOR_PID);
  const baudRate = Number(process.env.MONITOR_SERIAL_BAUD ?? "115200") || 115200;

  let disposed = false;
  const opened = new Map<string, any>();

  const openPort = (path: string) => {
    if (disposed || opened.has(path)) return;

    let lineBuffer = "";
    const port = new SerialPortCtor({ path, baudRate, autoOpen: false });
    opened.set(path, port);

    port.on("data", (chunk: Uint8Array) => {
      lineBuffer += Buffer.from(chunk).toString("utf8");
      if (lineBuffer.length > 4096) {
        lineBuffer = lineBuffer.slice(-2048);
      }

      const lines = lineBuffer.split(/\r?\n/);
      lineBuffer = lines.pop() ?? "";
      for (const line of lines) {
        for (const event of parseDongleTelemetryLine(line)) {
          publish(event);
        }
      }
    });

    port.on("error", () => {
      // Keep the monitor app alive if the CDC interface is busy or removed.
    });
    port.on("close", () => {
      opened.delete(path);
    });

    port.open((err: unknown) => {
      if (err || disposed) {
        opened.delete(path);
        try {
          port.close();
        } catch (_closeErr) {
          // ignore
        }
      }
    });
  };

  const scan = async () => {
    if (disposed) return;
    try {
      const ports = await SerialPortCtor.list();
      for (const port of ports) {
        if (port?.path && portMatches(port, explicitPaths, targetVid, targetPid)) {
          openPort(port.path);
        }
      }
    } catch (_err) {
      // optional source: failure to enumerate CDC should not break HID monitoring
    }
  };

  void scan();
  const timer = setInterval(() => {
    void scan();
  }, 2000);

  return () => {
    disposed = true;
    clearInterval(timer);
    for (const port of opened.values()) {
      try {
        port.close();
      } catch (_err) {
        // ignore
      }
    }
    opened.clear();
  };
}
