import type { MonitorEvent } from "../pipeline/types";

export interface ParsedDongleFrame {
  seq?: number;
  mode?: "USB" | "RF24G";
  state?: "Disconnected" | "Connecting" | "Connected" | "Error";
  targetRateHz?: number;
  actualRateHz?: number;
  deviceToUsbSubmitUs?: number;
  deviceToRfUs?: number;
  rfToUsbSubmitUs?: number;
  errorCode?: string;
  errorMessage?: string;
}

/**
 * Phase 1 简化解析器:
 * 输入一行文本，输出统一监控事件列表。
 * 约定 dongle/app 侧后续输出:
 * MON|TYPE=STATUS|MODE=RF24G|STATE=Connected|TARGET=2000|ACTUAL=1980
 * MON|TYPE=LATENCY|SEQ=12|D2U=850|D2R=410|R2U=220
 * MON|TYPE=ERROR|SRC=DONGLE|CODE=RF_CRC_FAIL|MSG=crc mismatch
 */
export function parseDongleTelemetryLine(line: string, timestampMs = Date.now()): MonitorEvent[] {
  const text = line.trim();
  if (!text.startsWith("MON|")) {
    return [];
  }

  const map = new Map<string, string>();
  const parts = text.split("|").slice(1);
  for (const part of parts) {
    const idx = part.indexOf("=");
    if (idx <= 0) continue;
    map.set(part.substring(0, idx), part.substring(idx + 1));
  }

  const type = map.get("TYPE");
  if (type === "STATUS") {
    return [{
      kind: "device_status",
      timestampMs,
      mode: (map.get("MODE") as "USB" | "RF24G") ?? "USB",
      state: (map.get("STATE") as "Disconnected" | "Connecting" | "Connected" | "Error") ?? "Disconnected",
      targetRateHz: Number(map.get("TARGET") ?? "0"),
      actualRateHz: Number(map.get("ACTUAL") ?? "0"),
    }];
  }

  if (type === "LATENCY") {
    const seq = Number(map.get("SEQ") ?? "0");
    return [{
      kind: "latency",
      timestampMs,
      seq,
      deviceToUsbSubmitUs: Number(map.get("D2U") ?? "0") || undefined,
      deviceToRfUs: Number(map.get("D2R") ?? "0") || undefined,
      rfToUsbSubmitUs: Number(map.get("R2U") ?? "0") || undefined,
    }];
  }

  if (type === "ERROR") {
    return [{
      kind: "error",
      timestampMs,
      source: map.get("SRC") ?? "UNKNOWN",
      code: map.get("CODE") ?? "UNKNOWN",
      level: "ERROR",
      message: map.get("MSG") ?? "unknown error",
    }];
  }

  return [];
}
