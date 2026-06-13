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

const RFH_TMOS_TICK_MS = 0.625;
const RFH_SILENT_TICKS_INVALID = 0xffff;

let lastRfHopHidTimestampMs = 0;
let lastLegacyRfHopRxCount = 0;
let lastLegacyRfHopExpectedCount = 0;
let haveLastLegacyRfHopTotals = false;

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

function isRfChannel(value: number) {
  return value >= 4 && value <= 39;
}

function legacyWindowCounts(
  targetRateHz: number,
  elapsedMs: number,
  totalRxCount: number,
  totalExpectedCount: number,
  lossPermille: number,
) {
  const derivedExpected = expectedForWindow(targetRateHz, elapsedMs);
  let sampleCount = 0;
  let expectedCount = derivedExpected;

  if (
    haveLastLegacyRfHopTotals &&
    totalRxCount >= lastLegacyRfHopRxCount &&
    totalExpectedCount >= lastLegacyRfHopExpectedCount
  ) {
    const deltaRx = totalRxCount - lastLegacyRfHopRxCount;
    const deltaExpected = totalExpectedCount - lastLegacyRfHopExpectedCount;
    if (deltaExpected > 0 && deltaExpected <= Math.max(derivedExpected * 4, 64)) {
      expectedCount = deltaExpected;
      sampleCount = Math.min(deltaRx, expectedCount);
    }
  }

  if (sampleCount === 0 && expectedCount > 0) {
    sampleCount = Math.max(0, Math.min(expectedCount, Math.round(expectedCount * (1000 - lossPermille) / 1000)));
  }

  lastLegacyRfHopRxCount = totalRxCount;
  lastLegacyRfHopExpectedCount = totalExpectedCount;
  haveLastLegacyRfHopTotals = true;
  return { sampleCount, expectedCount };
}

function legacyElapsedMs(rawElapsedMs: number, timestampMs: number) {
  if (rawElapsedMs > 0 && rawElapsedMs <= 1000) {
    return rawElapsedMs;
  }
  const hostElapsedMs = lastRfHopHidTimestampMs > 0 ? timestampMs - lastRfHopHidTimestampMs : 0;
  return hostElapsedMs >= 20 && hostElapsedMs <= 1000 ? hostElapsedMs : 100;
}

function parseRfHopHidTelemetryFrame(view: DataView, report: Uint8Array, timestampMs: number): MonitorEvent[] {
  const seq = view.getUint32(4, true);
  const rawElapsedMs = view.getUint16(8, true);
  const targetRateHz = view.getUint16(10, true);
  const rxCount = view.getUint32(12, true);
  const rawExpectedCount = view.getUint32(16, true);
  const rawLossPermille = view.getUint16(20, true);
  const rawState = view.getUint8(22);
  const channel = view.getUint8(23);
  const rawOldChannel = view.getUint8(24);
  const rawTargetChannel = view.getUint8(25);
  const rawHopEvents = view.getUint8(27);
  const rawErrorEvents = view.getUint8(28);
  const rawHopEventCode = view.getUint8(29);
  const legacyFixedLayout = rawState === 1;
  const state = legacyFixedLayout ? 2 : rawState;
  const oldChannel = legacyFixedLayout ? channel : rawOldChannel;
  const targetChannel = legacyFixedLayout ? channel : rawTargetChannel;
  const hopEvents = legacyFixedLayout ? 0 : rawHopEvents;
  const errorEvents = legacyFixedLayout ? 0 : rawErrorEvents;
  const hopEventCode = legacyFixedLayout ? 0 : rawHopEventCode;
  const hopEventValue = legacyFixedLayout ? 0 : view.getUint16(30, true);
  const hopEvent = hopEventCode === 1 ? "start" : hopEventCode === 2 ? "finish" : undefined;
  const rawSilentTicks = !legacyFixedLayout && hopEventCode === 0 ? hopEventValue : undefined;
  const maxSilentTicks =
    typeof rawSilentTicks === "number" && rawSilentTicks !== RFH_SILENT_TICKS_INVALID
      ? rawSilentTicks
      : undefined;
  const maxSilentMs =
    typeof maxSilentTicks === "number" ? Math.round(maxSilentTicks * RFH_TMOS_TICK_MS) : undefined;
  const normalized = legacyFixedLayout
    ? { elapsedMs: legacyElapsedMs(rawElapsedMs, timestampMs), expectedCount: 0 }
    : normalizeRfHopWindow(timestampMs, targetRateHz, rawElapsedMs, rxCount, rawExpectedCount);
  const elapsedMs = normalized.elapsedMs;
  const windowCounts = legacyFixedLayout
    ? legacyWindowCounts(targetRateHz, elapsedMs, rxCount, rawExpectedCount, rawLossPermille)
    : {
        sampleCount:
          normalized.expectedCount > 0 ? Math.min(rxCount, normalized.expectedCount) : rxCount,
        expectedCount: normalized.expectedCount,
      };
  if (!legacyFixedLayout) {
    haveLastLegacyRfHopTotals = false;
  }
  const sampleCount = windowCounts.sampleCount;
  const expectedCount = windowCounts.expectedCount;
  const actualRateHz = elapsedMs > 0 ? (sampleCount * 1000) / elapsedMs : 0;
  const lossPermille = expectedCount > 0 ? calcLossPermille(sampleCount, expectedCount) : rawLossPermille;
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
      sampleCount,
      expectedCount,
      sampleWindowMs: elapsedMs,
      rateHz: actualRateHz,
      lossPermille,
      channelNumber: channel,
      rfStateCode: stateCode,
      oldChannelNumber: oldChannel,
      targetChannelNumber: targetChannel,
      hopEvent,
      hopEventValue: hopEvent ? hopEventValue : undefined,
      hopScorePermille: hopEvent === "start" ? hopEventValue : undefined,
      hopDurationMs: hopEvent === "finish" ? hopEventValue : undefined,
      maxSilentTicks,
      maxSilentMs,
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

function parseRfHopScoreFrame(view: DataView, report: Uint8Array, timestampMs: number): MonitorEvent[] {
  const seq = view.getUint32(4, true);
  const count = Math.min(view.getUint8(8), 4);
  const activeChannel = view.getUint8(21);
  const channelScores: Array<{ channel: number; score: number }> = [];

  for (let i = 0; i < count; i++) {
    const offset = 9 + i * 3;
    const channel = view.getUint8(offset);
    const score = view.getUint16(offset + 1, true);
    channelScores.push({ channel, score });
  }

  channelScores.sort((a, b) => a.score - b.score || a.channel - b.channel);
  const active = channelScores.find((entry) => entry.channel === activeChannel);

  return [
    {
      kind: "packet",
      timestampMs,
      channel: "RF",
      direction: "RX",
      seq,
      messageType: "RFH_RHS1_SCORE",
      payloadLen: report.length,
      channelNumber: activeChannel,
      channelScores,
      activeChannelScore: active?.score,
    },
  ];
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
  if (magic === 0x31534852) {
    return parseRfHopScoreFrame(view, report, timestampMs);
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
