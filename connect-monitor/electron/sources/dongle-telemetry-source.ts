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

let rfHopCumulativeRx = 0;
let lastRfHopR5AtMs = 0;

function parseKvLine(text: string): Map<string, string> {
  const map = new Map<string, string>();
  const parts = text.split(/\s+/).slice(1);
  for (const part of parts) {
    const idx = part.indexOf("=");
    if (idx <= 0) continue;
    map.set(part.substring(0, idx), part.substring(idx + 1));
  }
  return map;
}

function parsePair(value: string | undefined): [number, number] {
  if (!value) return [0, 0];
  const [a, b] = value.split("/");
  return [Number(a ?? "0") || 0, Number(b ?? "0") || 0];
}

function parseQuad(value: string | undefined): [number, number, number, number] {
  if (!value) return [0, 0, 0, 0];
  const parts = value.split("/");
  return [
    Number(parts[0] ?? "0") || 0,
    Number(parts[1] ?? "0") || 0,
    Number(parts[2] ?? "0") || 0,
    Number(parts[3] ?? "0") || 0,
  ];
}

function parseCompactNumberToken(token: string | undefined, prefix: string): number {
  if (!token?.startsWith(prefix)) return 0;
  const n = Number(token.slice(prefix.length));
  return Number.isFinite(n) ? n : 0;
}

function parseCompactPairToken(token: string | undefined, prefix: string): [number, number] {
  if (!token?.startsWith(prefix)) return [0, 0];
  return parsePair(token.slice(prefix.length));
}

function parseCompactTripleToken(token: string | undefined, prefix: string): [number, number, number] {
  if (!token?.startsWith(prefix)) return [0, 0, 0];
  const parts = token.slice(prefix.length).split("/");
  return [
    Number(parts[0] ?? "0") || 0,
    Number(parts[1] ?? "0") || 0,
    Number(parts[2] ?? "0") || 0,
  ];
}

function findCompactToken(parts: string[], prefix: string): string | undefined {
  return parts.find((part) => part.startsWith(prefix));
}

function inferRfTargetRateHz(expectedCount: number): number {
  const perFiveSeconds = expectedCount / 5;
  if (perFiveSeconds >= 30000 / 5) return 8000;
  if (perFiveSeconds >= 15000 / 5) return 4000;
  if (perFiveSeconds >= 7500 / 5) return 2000;
  return 1000;
}

function rfStateToLinkState(state: string): "Disconnected" | "Connecting" | "Connected" | "Error" {
  if (state === "M") return "Connected";
  if (state === "D") return "Connecting";
  if (state === "C") return "Connected";
  if (state === "U") return "Disconnected";
  if (state === "PA" || state === "HR" || state === "CA" || state === "RD") return "Connecting";
  return "Error";
}

function parseChannelNumber(channelText: string | undefined): number | undefined {
  if (!channelText) return undefined;
  const first = channelText.split(/[/>]/)[0];
  const n = Number(first);
  return Number.isFinite(n) ? n : undefined;
}

function parseRfHopR5Line(text: string, timestampMs: number): MonitorEvent[] {
  const map = parseKvLine(text);
  const stateCode = map.get("S") ?? "U";
  const channelText = map.get("C") ?? "";
  const lossPermille = Number(map.get("L") ?? "1000") || 0;
  const [rxCount, expectedCount] = parsePair(map.get("P"));
  const ackCount = Number(map.get("A") ?? "0") || 0;
  const unconnectedCount = Number(map.get("U") ?? "0") || 0;
  const errorCount = Number(map.get("E") ?? "0") || 0;
  const targetRateHz = inferRfTargetRateHz(expectedCount);

  const elapsedMs = lastRfHopR5AtMs > 0 ? timestampMs - lastRfHopR5AtMs : 0;
  lastRfHopR5AtMs = timestampMs;
  const actualRateHz =
    elapsedMs >= 1000 && elapsedMs <= 10000
      ? (rxCount * 1000) / elapsedMs
      : expectedCount > 0
        ? (rxCount * targetRateHz) / expectedCount
        : 0;

  rfHopCumulativeRx += rxCount;
  const channelNumber = parseChannelNumber(channelText);
  const events: MonitorEvent[] = [
    {
      kind: "device_status",
      timestampMs,
      mode: "RF24G",
      state: rfStateToLinkState(stateCode),
      targetRateHz,
      actualRateHz,
    },
    {
      kind: "packet",
      timestampMs,
      channel: "RF",
      direction: "RX",
      seq: rfHopCumulativeRx,
      messageType: `RFH_R5_${stateCode}`,
      payloadLen: 0,
      sampleCount: rxCount,
      expectedCount,
      sampleWindowMs: elapsedMs || 5000,
      rateHz: actualRateHz,
      lossPermille,
      channelNumber,
    },
    {
      kind: "latency",
      timestampMs,
      seq: rfHopCumulativeRx,
    },
  ];

  if (errorCount > 0 || unconnectedCount > 0) {
    events.push({
      kind: "error",
      timestampMs,
      source: "RF_PHY_HOP_RX",
      code: errorCount > 0 ? "RFH_R5_ERROR" : "RFH_R5_UNCONNECTED",
      level: errorCount > 0 ? "WARN" : "INFO",
      message: `state=${stateCode} ch=${channelText} loss=${lossPermille} rx=${rxCount}/${expectedCount} ack=${ackCount} unconnected=${unconnectedCount} errors=${errorCount}`,
      count: errorCount || unconnectedCount,
    });
  }

  return events;
}

function parseRfHopR8Line(text: string, timestampMs: number): MonitorEvent[] {
  const parts = text.trim().split(/\s+/);
  const configCode = parseCompactNumberToken(findCompactToken(parts, "c"), "c");
  const stateCode = findCompactToken(parts, "S")?.slice(1) || "M";
  const connectStage = findCompactToken(parts, "g")?.slice(1);
  const channelText = findCompactToken(parts, "h")?.slice(1) ?? "";
  const [channelRaw, targetRaw] = channelText.split(">");
  const channelNumber = Number(channelRaw);
  const targetChannelNumber = Number(targetRaw);
  const targetRateHz = parseCompactNumberToken(findCompactToken(parts, "hz"), "hz") || 8000;
  const dataOk = parseCompactNumberToken(findCompactToken(parts, "d"), "d");
  const seqGap = parseCompactNumberToken(findCompactToken(parts, "gap"), "gap");
  const ackReq = parseCompactNumberToken(findCompactToken(parts, "q"), "q");
  const [ackOk, ackFail] = parseCompactPairToken(findCompactToken(parts, "a"), "a");
  const [crcErr, typeErr] = parseCompactPairToken(findCompactToken(parts, "e"), "e");
  const pendingDrop = parseCompactNumberToken(findCompactToken(parts, "p"), "p");
  const [pendingCurrent, pendingMax] = parseCompactPairToken(findCompactToken(parts, "w"), "w");
  const rssiText = findCompactToken(parts, "rssi")?.slice("rssi".length);
  const hopEvents = parseCompactNumberToken(findCompactToken(parts, "H"), "H");
  const [rxRet, txStartRet, txParmRet] = parseCompactTripleToken(findCompactToken(parts, "x"), "x");
  const rxActive = parseCompactNumberToken(findCompactToken(parts, "v"), "v");
  const expectedCount = dataOk + seqGap + crcErr + typeErr;
  const lossPermille =
    expectedCount > 0
      ? Math.min(1000, Math.round(((seqGap + crcErr + typeErr) * 1000) / expectedCount))
      : undefined;
  const actualRateHz = dataOk / 5;

  const packet: MonitorEvent = {
    kind: "packet",
    timestampMs,
    channel: "RF",
    direction: "RX",
    messageType: `RFH_R8_${stateCode || "M"}`,
    payloadLen: 0,
    sampleCount: dataOk,
    expectedCount,
    sampleWindowMs: 5000,
    rateHz: actualRateHz,
    lossPermille,
    channelNumber: Number.isFinite(channelNumber) ? channelNumber : undefined,
    rfStateCode: stateCode || "M",
    targetChannelNumber: Number.isFinite(targetChannelNumber) ? targetChannelNumber : undefined,
    unconnectedEvents: stateCode === "D" ? 1 : 0,
    errorEvents: crcErr + typeErr + ackFail,
    airPendingDrop: pendingDrop,
    airPendingCurrent: pendingCurrent,
    airPendingMax: pendingMax,
    airWindowRxOk: dataOk,
    airWindowExpected: expectedCount,
    airWindowErrors: seqGap + crcErr + typeErr,
    airWindowCrcErrors: crcErr,
    airWindowSeqGaps: seqGap,
    airWindowTypeErrors: typeErr,
  };

  const events: MonitorEvent[] = [
    {
      kind: "device_status",
      timestampMs,
      mode: "RF24G",
      state: rfStateToLinkState(stateCode),
      targetRateHz,
      actualRateHz,
    },
    packet,
    {
      kind: "latency",
      timestampMs,
      seq: dataOk,
    },
  ];

  const errorCount = crcErr + typeErr + ackFail + (configCode === 0 ? 0 : 1);
  if (errorCount > 0 || hopEvents > 0) {
    events.push({
      kind: "error",
      timestampMs,
      source: "RF_PHY_HOP_RX",
      code: errorCount > 0 ? "RFH_R8_DIAG" : "RFH_R8_HOP",
      level: errorCount > 0 ? "WARN" : "INFO",
      message: `state=${stateCode} stage=${connectStage ?? "-"} ch=${channelText} hz=${targetRateHz} data=${dataOk} gap=${seqGap} ack=${ackReq}/${ackOk}/${ackFail} err=${crcErr}/${typeErr} pend=${pendingCurrent}/${pendingMax} drop=${pendingDrop} rssi=${rssiText ?? "-"} hop=${hopEvents} ret=${rxRet}/${txStartRet}/${txParmRet} active=${rxActive}`,
      count: errorCount || hopEvents,
    });
  }

  return events;
}

function parseRfHopRdLine(text: string, timestampMs: number): MonitorEvent[] {
  const map = parseKvLine(text);
  const [armTry, armFail] = parsePair(map.get("A"));
  const pParts = (map.get("P") ?? "").split("/");
  const rxOk = Number(pParts[0] ?? "0") || 0;
  const crcErr = Number(pParts[1] ?? "0") || 0;
  const rxTimeout = Number(pParts[2] ?? "0") || 0;
  const [badLen, badType, badConnect, ignoredData] = parseQuad(map.get("B"));
  const [ackTry, ackFail] = parsePair(map.get("K"));
  const [appTimeout, dataResync] = parsePair(map.get("U"));

  const errorCount = armFail + crcErr + rxTimeout + badLen + badType + badConnect + ignoredData + ackFail + appTimeout + dataResync;
  const events: MonitorEvent[] = [
    {
      kind: "packet",
      timestampMs,
      channel: "RF",
      direction: "RX",
      messageType: "RFH_RD",
      payloadLen: 0,
      sampleCount: rxOk,
      expectedCount: armTry,
    },
  ];

  events.push({
    kind: "error",
    timestampMs,
    source: "RF_PHY_HOP_RX",
    code: "RFH_RD_DIAG",
    level: errorCount > 0 ? "WARN" : "INFO",
    message: `arm=${armTry}/${armFail} pkt=${rxOk}/${crcErr}/${rxTimeout} bad=${badLen}/${badType}/${badConnect}/${ignoredData} ack=${ackTry}/${ackFail} stale=${appTimeout}/${dataResync}`,
    count: errorCount,
  });

  return events;
}

/**
 * Phase 1 simplified parser:
 * Accepts one text line and returns normalized monitor events.
 * Expected dongle/application output:
 * MON|TYPE=STATUS|MODE=RF24G|STATE=Connected|TARGET=2000|ACTUAL=1980
 * MON|TYPE=LATENCY|SEQ=12|D2U=850|D2R=410|R2U=220
 * MON|TYPE=ERROR|SRC=DONGLE|CODE=RF_CRC_FAIL|MSG=crc mismatch
 */
export function parseDongleTelemetryLine(line: string, timestampMs = Date.now()): MonitorEvent[] {
  const text = line.trim();
  if (text.startsWith("R8 ")) {
    return parseRfHopR8Line(text, timestampMs);
  }
  if (text.startsWith("R5 ")) {
    return parseRfHopR5Line(text, timestampMs);
  }
  if (text.startsWith("RD ")) {
    return parseRfHopRdLine(text, timestampMs);
  }

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
