import type { MonitorEvent } from "../pipeline/types";

function rfHopStateCode(state: number): string {
  const map: Record<number, string> = {
    0: "U",
    1: "PA",
    2: "C",
    3: "HR",
    4: "CA",
    5: "RP",
    6: "RC",
  };
  return map[state] ?? "ERR";
}

function rfHopStateToLinkState(state: number): "Disconnected" | "Connecting" | "Connected" | "Error" {
  if (state === 2) return "Connected";
  if (state === 0) return "Disconnected";
  if (state === 1 || state === 3 || state === 4 || state === 5 || state === 6) return "Connecting";
  return "Error";
}

let lastRfHopHidTimestampMs = 0;

function expectedForWindow(targetRateHz: number, elapsedMs: number) {
  if (targetRateHz <= 0 || elapsedMs <= 0) return 0;
  return Math.max(1, Math.round((targetRateHz * elapsedMs) / 1000));
}

function normalizeRfHopWindow(
  timestampMs: number,
  targetRateHz: number,
  elapsedMs: number,
  rxCount: number,
  expectedCount: number,
) {
  const derivedExpected = expectedForWindow(targetRateHz, elapsedMs);
  const expectedSane =
    elapsedMs > 0 &&
    elapsedMs <= 1000 &&
    expectedCount > 0 &&
    derivedExpected > 0 &&
    Math.abs(expectedCount - derivedExpected) <= Math.max(64, derivedExpected * 0.25);

  if (expectedSane) {
    return { elapsedMs, expectedCount };
  }

  const hostElapsedMs = lastRfHopHidTimestampMs > 0 ? timestampMs - lastRfHopHidTimestampMs : 0;
  const inferredElapsedMs =
    hostElapsedMs >= 20 && hostElapsedMs <= 1000
      ? hostElapsedMs
      : targetRateHz > 0 && rxCount > 0
        ? Math.max(1, Math.round((rxCount * 1000) / targetRateHz))
        : 100;
  return {
    elapsedMs: inferredElapsedMs,
    expectedCount: expectedForWindow(targetRateHz, inferredElapsedMs),
  };
}

function calcLossPermille(rxCount: number, expectedCount: number) {
  if (expectedCount <= 0) return 0;
  if (rxCount >= expectedCount) return 0;
  return Math.min(1000, Math.round(((expectedCount - rxCount) * 1000) / expectedCount));
}

function parseRfHopHidTelemetryFrame(view: DataView, report: Uint8Array, timestampMs: number): MonitorEvent[] {
  const seq = view.getUint32(4, true);
  const rawElapsedMs = view.getUint16(8, true);
  const targetRateHz = view.getUint16(10, true);
  const rxCount = view.getUint32(12, true);
  const rawExpectedCount = view.getUint32(16, true);
  const rawLossPermille = view.getUint16(20, true);
  const state = view.getUint8(22);
  const channel = view.getUint8(23);
  const oldChannel = view.getUint8(24);
  const targetChannel = view.getUint8(25);
  const hopEvents = view.getUint8(27);
  const errorEvents = view.getUint8(28);
  const normalized = normalizeRfHopWindow(timestampMs, targetRateHz, rawElapsedMs, rxCount, rawExpectedCount);
  const elapsedMs = normalized.elapsedMs;
  const expectedCount = normalized.expectedCount;
  const actualRateHz = elapsedMs > 0 ? (rxCount * 1000) / elapsedMs : 0;
  const lossPermille = expectedCount > 0 ? calcLossPermille(rxCount, expectedCount) : rawLossPermille;
  const stateCode = rfHopStateCode(state);
  lastRfHopHidTimestampMs = timestampMs;

  const events: MonitorEvent[] = [
    {
      kind: "device_status",
      timestampMs,
      mode: "RF24G",
      state: rfHopStateToLinkState(state),
      targetRateHz,
      actualRateHz,
    },
    {
      kind: "packet",
      timestampMs,
      channel: "RF",
      direction: "RX",
      seq,
      messageType: `RFH_RHM1_${stateCode}`,
      payloadLen: report.length,
      sampleCount: rxCount,
      expectedCount,
      sampleWindowMs: elapsedMs,
      rateHz: actualRateHz,
      lossPermille,
      channelNumber: channel,
      rfStateCode: stateCode,
      oldChannelNumber: oldChannel,
      targetChannelNumber: targetChannel,
      unconnectedEvents: 0,
      errorEvents,
    },
  ];

  if (hopEvents > 0 || errorEvents > 0) {
    events.push({
      kind: "error",
      timestampMs,
      source: "RF_PHY_HOP_RX",
      code: errorEvents > 0 ? "RFH_HID_ERROR" : "RFH_HID_HOP",
      level: errorEvents > 0 ? "WARN" : "INFO",
      message: `state=${stateCode} ch=${channel} old=${oldChannel} target=${targetChannel} loss=${lossPermille} rx=${rxCount}/${expectedCount} hop=${hopEvents} errors=${errorEvents}`,
      count: errorEvents || hopEvents,
    });
  }

  return events;
}

/**
 * Parse the dongle HID telemetry frame (DMN1, 32 bytes).
 * Frame layout is defined in dongle/src/dongle_telemetry.c.
 */
export function parseDongleHidTelemetryFrame(report: Uint8Array, timestampMs = Date.now()): MonitorEvent[] {
  if (report.length < 32) {
    return [];
  }

  const view = new DataView(report.buffer, report.byteOffset, report.byteLength);
  const magic = view.getUint32(0, true);
  if (magic === 0x314d4852) {
    return parseRfHopHidTelemetryFrame(view, report, timestampMs);
  }

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
