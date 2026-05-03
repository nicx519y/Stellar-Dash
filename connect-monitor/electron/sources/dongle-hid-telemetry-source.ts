import type { MonitorEvent } from "../pipeline/types";

/**
 * 解析 dongle 侧 HID telemetry 帧（DMN1, 32 bytes）。
 * 帧定义见 dongle/src/dongle_telemetry.c。
 */
export function parseDongleHidTelemetryFrame(report: Uint8Array, timestampMs = Date.now()): MonitorEvent[] {
  if (report.length < 32) {
    return [];
  }

  const view = new DataView(report.buffer, report.byteOffset, report.byteLength);
  const magic = view.getUint32(0, true);
  if (magic !== 0x314e4d44) {
    return [];
  }

  const rxCount = view.getUint32(8, true);
  const txReportCount = view.getUint32(12, true);
  const invalidCount = view.getUint32(16, true);
  const telemetryDropCount = view.getUint32(20, true);
  const staleCount = view.getUint16(24, true);
  const latestSeq = view.getUint8(26);
  const dongleState = view.getUint8(27);
  const flags = view.getUint8(28);

  const stateMap: Record<number, "Disconnected" | "Connecting" | "Connected" | "Error"> = {
    0: "Disconnected",
    1: "Connecting",
    2: "Connecting",
    3: "Connecting",
    4: "Connected",
  };

  return [
    {
      kind: "device_status",
      timestampMs,
      mode: "RF24G",
      state: stateMap[dongleState] ?? "Error",
      targetRateHz: 8000,
      actualRateHz: 0,
    },
    {
      kind: "packet",
      timestampMs,
      channel: "RF",
      direction: "RX",
      seq: latestSeq,
      messageType: "DONGLE_DMN1",
      payloadLen: report.length,
    },
    {
      kind: "error",
      timestampMs,
      source: "DONGLE_MONITOR",
      code: "STATS",
      level: "INFO",
      message: `rx=${rxCount} tx=${txReportCount} invalid=${invalidCount} drop=${telemetryDropCount} stale=${staleCount} flags=0x${flags.toString(16)}`,
    },
  ];
}
