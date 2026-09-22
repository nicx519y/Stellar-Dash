import type { LinkState, MonitorEvent } from "../pipeline/types";

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

function rfHopStateToLinkState(state: number): LinkState {
  if (state === 2 || state === 3) return "Connected"; // RX HR retains a live DATA link.
  if (state === 0) return "Disconnected";
  if (state === 1) return "Pairing";
  if (state === 4) return "Connecting";
  if (state === 5 || state === 6) return "Reconnecting";
  return "Error";
}

const RFH_INPUT_FLAG_BATTERY_CODE = 0x04;
const RFH_INPUT_FLAG_BATTERY_H2 = 0x08;
const RFH_INPUT_BATTERY_BASE_MV = 3000;
const RFH_INPUT_BATTERY_STEP_MV = 10;

function decodeRfHopBatteryMv(code: number) {
  if (code <= 0) return 0;
  return RFH_INPUT_BATTERY_BASE_MV + (code - 1) * RFH_INPUT_BATTERY_STEP_MV;
}

function estimateSocPercent(mv: number) {
  if (mv <= 0) return 0;
  if (mv <= 3300) return 0;
  if (mv >= 4200) return 100;
  return Math.round(((mv - 3300) * 100) / 900);
}

function parseRfHopInputFrame(view: DataView, report: Uint8Array, timestampMs: number): MonitorEvent[] {
  const seq = view.getUint32(4, true);
  const inputKeyMask = view.getUint32(8, true);
  const inputSeq = view.getUint8(12);
  const inputFlags = view.getUint8(13);
  const state = view.getUint8(14);
  const channel = view.getUint8(15);
  const targetRateHz = view.getUint16(16, true);
  const airRateCode = view.getUint8(18);
  const airLinkActive = view.getUint8(19) !== 0;
  const airLastDataSeq = view.getUint8(20);
  const airPendingDrop = report.length >= 23 ? view.getUint16(21, true) : undefined;
  const airPendingCurrent = report.length >= 24 ? view.getUint8(23) : undefined;
  const airPendingMax = report.length >= 25 ? view.getUint8(24) : undefined;
  const airWindowRxOk = report.length >= 27 ? view.getUint16(25, true) : undefined;
  const airWindowExpected = report.length >= 29 ? view.getUint16(27, true) : undefined;
  const airWindowCrcErrors = report.length >= 31 ? view.getUint16(29, true) : undefined;
  const batteryCode = report.length >= 32 ? view.getUint8(31) : 0;
  const batteryValid = (inputFlags & RFH_INPUT_FLAG_BATTERY_CODE) !== 0 && batteryCode > 0;
  const activeBattery = (inputFlags & RFH_INPUT_FLAG_BATTERY_H2) !== 0 ? "H2" : "H1";
  const batteryMv = batteryValid ? decodeRfHopBatteryMv(batteryCode) : 0;
  const airWindowSeqGaps = batteryValid ? undefined : report.length >= 32 ? view.getUint8(31) : undefined;
  const airWindowTypeErrors = undefined;
  const airWindowTimeoutErrors = undefined;
  const airDiagLooksSane =
    typeof airPendingCurrent === "number" &&
    typeof airPendingMax === "number" &&
    airPendingMax <= 16 &&
    airPendingCurrent <= airPendingMax;
  const airWindowErrors =
    typeof airWindowCrcErrors === "number" ||
    typeof airWindowTypeErrors === "number" ||
    typeof airWindowTimeoutErrors === "number"
      ? (airWindowCrcErrors ?? 0) + (airWindowTypeErrors ?? 0) + (airWindowTimeoutErrors ?? 0)
      : undefined;
  const stateCode = rfHopStateCode(state);

  const events: MonitorEvent[] = [
    {
      kind: "packet",
      timestampMs,
      channel: "RF",
      direction: "RX",
      seq,
      messageType: `RFH_RHI1_${stateCode}`,
      payloadLen: report.length,
      payloadHex: hexReport(report),
      channelNumber: channel,
      rfStateCode: stateCode,
      rateHz: targetRateHz,
      inputKeyMask,
      inputSeq,
      inputFlags,
      airRateCode,
      airLastDataSeq,
      airLinkActive,
      airPendingDrop: airDiagLooksSane ? airPendingDrop : undefined,
      airPendingCurrent: airDiagLooksSane ? airPendingCurrent : undefined,
      airPendingMax: airDiagLooksSane ? airPendingMax : undefined,
      airWindowRxOk: airDiagLooksSane ? airWindowRxOk : undefined,
      airWindowExpected: airDiagLooksSane ? airWindowExpected : undefined,
      airWindowErrors: airDiagLooksSane ? airWindowErrors : undefined,
      airWindowCrcErrors: airDiagLooksSane ? airWindowCrcErrors : undefined,
      airWindowSeqGaps: airDiagLooksSane ? airWindowSeqGaps : undefined,
      airWindowTypeErrors: airDiagLooksSane ? airWindowTypeErrors : undefined,
      airWindowTimeoutErrors: airDiagLooksSane ? airWindowTimeoutErrors : undefined,
    },
  ];

  if (batteryValid) {
    events.push({
      kind: "power_status",
      timestampMs,
      h1Mv: activeBattery === "H1" ? batteryMv : 0,
      h2Mv: activeBattery === "H2" ? batteryMv : 0,
      batMv: batteryMv,
      socPercent: estimateSocPercent(batteryMv),
      activeBattery,
      chargeState: "Discharging",
      valid: true,
      lowBattery: batteryMv > 0 && batteryMv <= 3300,
    });
  }

  return events;
}

const RFH_TMOS_TICK_MS = 0.625;
const RFH_SILENT_TICKS_INVALID = 0xffff;

function hexReport(report: Uint8Array) {
  return Array.from(report, (byte) => byte.toString(16).padStart(2, "0").toUpperCase()).join(" ");
}

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
  return value >= 0 && value <= 39;
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
      payloadHex: hexReport(report),
      sampleCount,
      expectedCount,
      sampleWindowMs: elapsedMs,
      rateHz: actualRateHz,
      targetRateHz,
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
  const version = view.getUint8(31);
  const count = Math.min(view.getUint8(8), version === 1 ? 7 : 4);
  const activeChannel = version === 1 ? view.getUint8(30) : view.getUint8(21);
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
      payloadHex: hexReport(report),
      channelNumber: activeChannel,
      channelScores,
      activeChannelScore: active?.score,
    },
  ];
}

function parseRfHopRssiFrame(view: DataView, report: Uint8Array, timestampMs: number): MonitorEvent[] {
  const seq = view.getUint32(4, true);
  const samples = view.getUint16(8, true);
  const rssiAvg = view.getInt8(10);
  const rssiMin = view.getInt8(11);
  const rssiMax = view.getInt8(12);
  const rssiLast = view.getInt8(13);
  const state = view.getUint8(14);
  const channel = view.getUint8(15);
  const oldChannel = view.getUint8(16);
  const targetChannel = view.getUint8(17);
  const targetRateHz = view.getUint16(18, true);
  const airRateCode = view.getUint8(20);
  const airLinkActive = view.getUint8(21) !== 0;
  const stateCode = rfHopStateCode(state);

  return [
    {
      kind: "packet",
      timestampMs,
      channel: "RF",
      direction: "RX",
      seq,
      messageType: `RFH_RHR1_${stateCode}`,
      payloadLen: report.length,
      payloadHex: hexReport(report),
      channelNumber: channel,
      rfStateCode: stateCode,
      oldChannelNumber: oldChannel,
      targetChannelNumber: targetChannel,
      targetRateHz,
      airRateCode,
      airLinkActive,
      rssiSamples: samples,
      rssiAvg,
      rssiMin,
      rssiMax,
      rssiLast,
    },
  ];
}

function parseRfHopLatencyFrame(view: DataView, report: Uint8Array, timestampMs: number, hostMonoUs?: number): MonitorEvent[] {
  const seq = view.getUint32(4, true);
  const inputSeq = view.getUint8(8);
  const inputFlags = view.getUint8(9);
  const inputKeyMask = view.getUint32(10, true);
  const latencyUs = view.getUint32(14, true);
  const latencyStm32Us = view.getUint16(18, true);
  const latencyTxUs = view.getUint16(20, true);
  const latencyRxUs = view.getUint16(22, true);
  const latencyStageFlags = view.getUint8(24);
  const syncSeq = view.getUint8(25);
  const state = view.getUint8(27);
  const channel = view.getUint8(28);
  const airRateCode = view.getUint8(29);
  const airLinkActive = view.getUint8(30) !== 0;
  const stateCode = rfHopStateCode(state);

  return [
    {
      kind: "packet",
      timestampMs,
      channel: "RF",
      direction: "RX",
      seq,
      messageType: "RFH_RHL1",
      payloadLen: report.length,
      payloadHex: hexReport(report),
      channelNumber: channel,
      rfStateCode: stateCode,
      inputKeyMask,
      inputSeq,
      inputFlags,
      airRateCode,
      airLinkActive,
      hostMonoUs,
      sampleTickUs: latencyUs,
      latencyUs,
      latencyStm32Us,
      latencyTxUs,
      latencyRxUs,
      latencyStageFlags,
      syncSeq,
    },
  ];
}

function parseRfHopLatencyV2Frame(view: DataView, report: Uint8Array, timestampMs: number, hostMonoUs?: number): MonitorEvent[] {
  const seq = view.getUint32(4, true);
  const inputSeq = view.getUint8(8);
  const inputFlags = view.getUint8(9);
  const inputKeyMask = view.getUint32(10, true);
  const latencyStm32Us = view.getUint16(14, true);
  const latencyTxUs = view.getUint16(16, true);
  const latencyRxUs = view.getUint16(18, true);
  const latencyRxIrqUs = view.getUint16(20, true);
  const latencyRxDecodeUs = view.getUint16(22, true);
  const latencyRxEpWaitUs = view.getUint16(24, true);
  const latencyRxSubmitUs = view.getUint16(26, true);
  const latencyStageFlags = view.getUint8(28);
  const state = view.getUint8(29);
  const channel = view.getUint8(30);
  const stateCode = rfHopStateCode(state);
  const latencyUs = latencyStm32Us + latencyTxUs + latencyRxUs;

  return [
    {
      kind: "packet",
      timestampMs,
      channel: "RF",
      direction: "RX",
      seq,
      messageType: "RFH_RHL2",
      payloadLen: report.length,
      payloadHex: hexReport(report),
      channelNumber: channel,
      rfStateCode: stateCode,
      inputKeyMask,
      inputSeq,
      inputFlags,
      hostMonoUs,
      sampleTickUs: latencyUs,
      latencyUs,
      latencyStm32Us,
      latencyTxUs,
      latencyRxUs,
      latencyRxIrqUs,
      latencyRxDecodeUs,
      latencyRxEpWaitUs,
      latencyRxSubmitUs,
      latencyStageFlags,
    },
  ];
}

function parseRfDiagnostics(view: DataView, report: Uint8Array, timestampMs: number): MonitorEvent[] {
  const version = view.getUint8(8), page = view.getUint8(9);
  if (version !== 1 || page > 3) return [];
  return [{
    kind: "packet", timestampMs, channel: "RF", direction: "RX",
    seq: view.getUint32(4, true), messageType: `RFH_RHD1_${page}`,
    payloadLen: report.length, payloadHex: hexReport(report),
    rfDiagnosticVersion: version, rfDiagnosticPage: page,
    rfStateCode: rfHopStateCode(view.getUint8(10)), channelNumber: view.getUint8(11),
    ...(page === 0 ? {
      rfReadyMs: view.getUint16(12, true),
      rfBuildId: view.getUint16(30, true) || undefined,
      usbReadyMs: view.getUint16(14, true) === 0xffff ? undefined : view.getUint16(14, true),
      rfConnectMs: view.getUint16(16, true), hopDurationMs: view.getUint16(18, true),
      rfConnectCount: view.getUint32(20, true), rfAckWatchdog: view.getUint32(24, true),
      airPendingMax: view.getUint8(28), airPendingCurrent: view.getUint8(29),
    } : page === 1 ? {
      rfAckLate: view.getUint32(12, true), rfAckDuplicates: view.getUint32(16, true),
      airPendingDrop: view.getUint32(20, true), rfInputEdgeDrop: view.getUint32(24, true),
      rfCrcTotal: view.getUint32(28, true),
    } : page === 2 ? {
      rfTxDiagnosticValid: view.getUint8(31) !== 0,
      rfTxWindowMs: view.getUint16(12, true), rfTxDue: view.getUint16(14, true),
      rfTxStarted: view.getUint16(16, true), rfTxDropped: view.getUint16(18, true),
      rfTxDiagnosticAgeMs: view.getUint16(20, true),
      rfAirMissingTotal: view.getUint32(22, true), rfAirReceivedTotal: view.getUint32(26, true),
      rfReceiverState: view.getUint8(30),
    } : {
      rfRxArmFailures: view.getUint32(12, true),
      rfRxRearmMaxUs: view.getUint16(16, true), rfRxCallbackMaxUs: view.getUint16(18, true),
      rfInputCommitMaxUs: view.getUint16(20, true), rfInputCaptureMaxUs: view.getUint16(22, true),
      rfAckSendFailures: view.getUint32(24, true), rfShortDecodedTotal: view.getUint32(28, true),
    }),
  }];
}

/**
 * Parse the dongle HID telemetry frame (DMN1, 32 bytes).
 * Frame layout is defined in dongle/src/dongle_telemetry.c.
 */
export function parseDongleHidTelemetryFrame(report: Uint8Array, timestampMs = Date.now(), hostMonoUs?: number): MonitorEvent[] {
  if (report.length < 32) {
    return [];
  }

  const view = new DataView(report.buffer, report.byteOffset, report.byteLength);
  const magic = view.getUint32(0, true);
  if(magic===0x34444652 || magic===0x34454652){
    if(report[5]!==2 || report[4]>1)return [];
    return [{kind:"packet",timestampMs,channel:"RF",direction:"RX",messageType:magic===0x34444652?"RF_FAST_STATUS":"RF_FAST_EVENT",
      payloadLen:32,rfDiagnosticVersion:4,
      ...(magic===0x34444652?{rfFast:{role:report[4],profile:report[5],testId:view.getUint16(6,true),
        state:report[8],requested:!!report[9],effective:!!report[10],reason:report[11],supportedRates:report[12],acceptedRates:report[13],
        sequence:report[15],successes:view.getUint32(16,true),failures:view.getUint32(20,true),elapsedUs:view.getUint32(24,true),overflow:view.getUint32(28,true)}}:
      {rfFastEvent:{role:report[4],sequence:view.getUint16(6,true),atUs:view.getUint32(8,true),plannedUs:view.getUint32(12,true),
        value:view.getUint32(16,true),generation:view.getUint32(20,true),testId:view.getUint16(24,true),event:report[26],channel:report[27],maintenanceUs:view.getUint32(28,true)}})}];
  }
  if (magic === 0x33464852) {
    const page = report[8];
    if (report[9] !== 3 && report[9] !== 4 || page > 5) return [];
    return [{ kind: "packet", timestampMs, channel: "RF", direction: "RX",
      seq: view.getUint32(4, true), messageType: `RFH_RHF3_${page}`,
      payloadLen: 32, payloadHex: hexReport(report), rfDiagnosticVersion: 3,
      rfChannel: { page, ageMs: view.getUint16(10, true),
        ...(page === 0 ? {
          state: report[13], primary: report[14], candidate: report[15], backups: [report[16], report[17]],
          probeDisabled: report[18], reason: report[19], maintenanceUs: view.getUint16(20, true),
          beforePermille: view.getUint16(22, true), afterPermille: view.getUint16(24, true),
          localCaps: report[26], peerCaps: report[27], switches: view.getUint32(28, true),
        } : page === 1 ? {
          probes: view.getUint32(12, true), failures: view.getUint32(16, true),
          probeFailures: view.getUint32(20, true), sampleCount: view.getUint32(24, true),
          probation: report[28] !== 0, sampleSource: report[29],
          scores: [{ channel: 10, lossPermille: view.getUint16(30, true) }],
        } : page === 2 ? {
          scores: [16,22,24,28,34,39].map((channel,i)=>({channel,lossPermille:view.getUint16(12+2*i,true)})),
          historyAgeMs: view.getUint16(24, true),
        } : page === 3 ? {
          lastGapUs: view.getUint32(12, true), maxGapUs: view.getUint32(16, true),
          firstPacketUs: view.getUint32(20, true), transitionMissing: view.getUint32(24, true),
          inputCoalesced: view.getUint32(28, true),
        } : page === 4 ? {
          versionMismatches: view.getUint32(12, true), receiverState: report[16], receiverChannel: report[17],
        } : {
          beforeGapUs: view.getUint32(12, true), transitionGapUs: view.getUint32(16, true),
          reservationFailures: view.getUint32(20, true), radioFailures: view.getUint32(24, true),
          candidateFailures: view.getUint32(28, true),
        }),
      },
    }];
  }
  if (magic === 0x33434852 || magic === 0x33454852 || magic === 0x34434852) {
    // Explicit v3 magic: never reinterpret an old duration as a clock stamp.
    let crc = 0;
    for (let i = 0; i < 31; i++) {
      crc ^= report[i];
      for (let bit = 0; bit < 8; bit++) crc = ((crc << 1) ^ ((crc & 128) ? 7 : 0)) & 255;
    }
    if (report[4] !== (magic === 0x34434852 ? 4 : 3) || crc !== report[31]) return [];
    const sync = magic !== 0x33454852;
    const mask = view.getUint32(8, true);
    return [{ kind: "packet", timestampMs, channel: "RF", direction: "RX",
      seq: report[5], inputSeq: report[5], payloadLen: 32, payloadHex: hexReport(report),
      messageType: sync ? (magic === 0x34434852 ? "RFH_RHC4" : "RFH_RHC3") : "RFH_RHE3", hostMonoUs,
      ...(magic === 0x34434852 ? {syncQueueWaitUs:view.getUint32(20,true)} : {}),
      traceDrops: view.getUint32(16, true),
      ...(sync ? { syncSeq: report[5], syncRxTickUs: mask, syncTxTickUs: view.getUint32(12, true) }
        : { inputKeyMask: mask & 0x3ffff, sampleTickUs: view.getUint32(12, true), traceBaseline: !!(mask & 0x40000000) }),
    }];
  }
  if(magic===0x32504852) return [{
    kind:"packet",timestampMs,channel:"RF",direction:"RX",messageType:"RFH_RHP2",payloadLen:32,payloadHex:hexReport(report),
    seq:view.getUint32(4,true),rfTx5ByteTotal:view.getUint32(8,true),rfTx7ByteTotal:view.getUint32(12,true),
    rfTx12ByteTotal:view.getUint32(16,true),rfAckReservedSlots:view.getUint32(20,true),
    rfControlGuardSlots:view.getUint32(24,true),rfTraceOverwrites:view.getUint32(28,true)
  }];
  if (magic === 0x31444852) return parseRfDiagnostics(view, report, timestampMs);
  if (magic === 0x314d4852) {
    return parseRfHopHidTelemetryFrame(view, report, timestampMs);
  }
  if (magic === 0x31534852) {
    return parseRfHopScoreFrame(view, report, timestampMs);
  }
  if (magic === 0x31524852) {
    return parseRfHopRssiFrame(view, report, timestampMs);
  }
  if (magic === 0x31494852) {
    return parseRfHopInputFrame(view, report, timestampMs);
  }
  if (magic === 0x314c4852) {
    return parseRfHopLatencyFrame(view, report, timestampMs, hostMonoUs);
  }
  if (magic === 0x324c4852) {
    return parseRfHopLatencyV2Frame(view, report, timestampMs, hostMonoUs);
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
      payloadHex: hexReport(report),
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
