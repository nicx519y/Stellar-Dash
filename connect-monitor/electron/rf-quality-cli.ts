import { parseDongleHidTelemetryFrame } from "./sources/dongle-hid-telemetry-source";
import type { DebugConfig, PacketEvent } from "../shared/monitor-types";

const CTL_MAGIC = 0x314c5443;
const CTL_VERSION = 1;
const CTL_FRAME_SIZE = 32;
const FLAG_HID_TELEMETRY = 0x01;
const FLAG_AUTO_HOP = 0x10;

type CliOptions = {
  durationMs: number;
  periodMs: 100 | 250 | 500 | 1000;
  json: boolean;
  list: boolean;
  configure: boolean;
  vid?: number;
  pid?: number;
  manualChannel: number | null;
};

type HidDeviceInfo = {
  path?: string;
  vendorId?: number;
  productId?: number;
  usagePage?: number;
  usage?: number;
  interface?: number;
  interfaceNumber?: number;
  manufacturer?: string;
  product?: string;
};

type Summary = {
  samples: number;
  scores: number;
  rxTotal: number;
  expectedTotal: number;
  lossPermilleSum: number;
  maxLossPermille: number;
  rateHzSum: number;
  rateSamples: number;
  channelSwitches: number;
  hopStarts: number;
  hopFinishes: number;
  errorEvents: number;
  maxSilentMs: number;
  lastChannel?: number;
  lastTargetChannel?: number;
  lastState?: string;
  latestScores?: Array<{ channel: number; score: number }>;
};

function parseNumber(value: string | undefined): number | undefined {
  if (!value) return undefined;
  const parsed = value.startsWith("0x") || value.startsWith("0X") ? Number.parseInt(value, 16) : Number(value);
  return Number.isFinite(parsed) ? parsed : undefined;
}

function parseArgs(argv: string[]): CliOptions {
  const options: CliOptions = {
    durationMs: 10_000,
    periodMs: 250,
    json: false,
    list: false,
    configure: true,
    manualChannel: null,
  };

  for (let i = 0; i < argv.length; i++) {
    const arg = argv[i];
    const next = argv[i + 1];
    if (arg === "--duration" || arg === "-d") {
      const value = parseNumber(next);
      if (value) options.durationMs = value;
      i++;
    } else if (arg === "--period") {
      const value = parseNumber(next);
      if (value === 100 || value === 250 || value === 500 || value === 1000) options.periodMs = value;
      i++;
    } else if (arg === "--vid") {
      options.vid = parseNumber(next);
      i++;
    } else if (arg === "--pid") {
      options.pid = parseNumber(next);
      i++;
    } else if (arg === "--manual-channel") {
      const value = parseNumber(next);
      options.manualChannel = typeof value === "number" ? value : null;
      i++;
    } else if (arg === "--json") {
      options.json = true;
    } else if (arg === "--list") {
      options.list = true;
    } else if (arg === "--no-config") {
      options.configure = false;
    }
  }
  return options;
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

function buildControlFrame(config: DebugConfig, seq: number): Buffer {
  const frame = Buffer.alloc(CTL_FRAME_SIZE);
  const flags = (config.hidTelemetryEnabled ? FLAG_HID_TELEMETRY : 0) |
    (config.autoHopEnabled ? FLAG_AUTO_HOP : 0);
  putU32LE(frame, 0, CTL_MAGIC);
  frame[4] = CTL_VERSION;
  frame[5] = seq & 0xff;
  frame[6] = 0;
  frame[7] = 1;
  putU32LE(frame, 8, flags);
  putU16LE(frame, 12, config.hidTelemetryEnabled ? config.hidPeriodMs : 0);
  putU16LE(frame, 14, crc16Ccitt(frame, 14));
  frame[16] = typeof config.manualChannel === "number" ? config.manualChannel : 0xff;
  return frame;
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

function textIncludes(value: unknown, needle: string): boolean {
  return typeof value === "string" && value.toLowerCase().includes(needle);
}

function isLikelyHBoxDevice(device: HidDeviceInfo): boolean {
  return textIncludes(device.manufacturer, "hbox") || textIncludes(device.product, "hbox");
}

function isLikelyTelemetryInterface(device: HidDeviceInfo): boolean {
  const interfaceNumber = typeof device.interface === "number" ? device.interface : device.interfaceNumber;
  if (device.usagePage === 0xff00) return true;
  if (interfaceNumber === 3 && isLikelyHBoxDevice(device)) return true;
  return false;
}

function isGenericDesktopController(device: HidDeviceInfo): boolean {
  return device.usagePage === 0x01 && (device.usage === 0x04 || device.usage === 0x05);
}

function matchesTarget(device: HidDeviceInfo, options: CliOptions): boolean {
  if (typeof options.vid === "number" && device.vendorId !== options.vid) return false;
  if (typeof options.pid === "number" && device.productId !== options.pid) return false;
  if (typeof options.vid === "number" || typeof options.pid === "number") return !isGenericDesktopController(device);

  const defaultId =
    (device.vendorId === 0x045e && device.productId === 0x02ff) ||
    (device.vendorId === 0x1a86 && device.productId === 0xfe0c);
  if (defaultId) return isLikelyTelemetryInterface(device);
  return isLikelyHBoxDevice(device) && isLikelyTelemetryInterface(device);
}

function fmtTime(ms: number): string {
  const d = new Date(ms);
  return `${d.toLocaleTimeString()}.${String(d.getMilliseconds()).padStart(3, "0")}`;
}

function fmtPermille(value: number | undefined): string {
  return typeof value === "number" ? `${(value / 10).toFixed(1)}%` : "-";
}

function packetLine(packet: PacketEvent): string {
  if (packet.messageType === "RFH_RHS1_SCORE") {
    const scores = packet.channelScores?.map((entry) => `${entry.channel}${entry.channel === packet.channelNumber ? "*" : ""}:${entry.score}`).join(" ") ?? "-";
    return `${fmtTime(packet.timestampMs)} SCORE active=${packet.channelNumber ?? "-"} score=${packet.activeChannelScore ?? "-"} ${scores}`;
  }

  const actualRate = typeof packet.rateHz === "number" ? Math.round(packet.rateHz) : "-";
  const targetRate = packet.targetRateHz ?? "-";
  const rx = packet.sampleCount ?? "-";
  const expected = packet.expectedCount ?? "-";
  const elapsed = packet.sampleWindowMs ?? "-";
  const channel = packet.channelNumber ?? "-";
  const target = packet.targetChannelNumber ?? "-";
  const old = packet.oldChannelNumber ?? "-";
  const event =
    packet.hopEvent === "start"
      ? `hop=start score=${packet.hopScorePermille ?? packet.hopEventValue ?? "-"}`
      : packet.hopEvent === "finish"
        ? `hop=finish ${packet.hopDurationMs ?? packet.hopEventValue ?? "-"}ms`
        : `silent=${packet.maxSilentMs ?? "-"}ms`;
  return `${fmtTime(packet.timestampMs)} state=${packet.rfStateCode ?? "-"} ch=${channel}->${target} old=${old} rate=${actualRate}/${targetRate} loss=${fmtPermille(packet.lossPermille)} rx=${rx}/${expected} win=${elapsed}ms err=${packet.errorEvents ?? 0} ${event} seq=${packet.seq ?? "-"}`;
}

function updateSummary(summary: Summary, packet: PacketEvent): void {
  if (packet.messageType === "RFH_RHS1_SCORE") {
    summary.scores++;
    summary.latestScores = packet.channelScores;
    return;
  }

  summary.samples++;
  summary.rxTotal += packet.sampleCount ?? 0;
  summary.expectedTotal += packet.expectedCount ?? 0;
  summary.lossPermilleSum += packet.lossPermille ?? 0;
  summary.maxLossPermille = Math.max(summary.maxLossPermille, packet.lossPermille ?? 0);
  if (typeof packet.rateHz === "number") {
    summary.rateHzSum += packet.rateHz;
    summary.rateSamples++;
  }
  if (
    typeof summary.lastChannel === "number" &&
    typeof packet.channelNumber === "number" &&
    summary.lastChannel !== packet.channelNumber
  ) {
    summary.channelSwitches++;
  }
  if (packet.hopEvent === "start") summary.hopStarts++;
  if (packet.hopEvent === "finish") summary.hopFinishes++;
  summary.errorEvents += packet.errorEvents ?? 0;
  summary.maxSilentMs = Math.max(summary.maxSilentMs, packet.maxSilentMs ?? 0);
  summary.lastChannel = packet.channelNumber;
  summary.lastTargetChannel = packet.targetChannelNumber;
  summary.lastState = packet.rfStateCode;
}

function buildSummary(): Summary {
  return {
    samples: 0,
    scores: 0,
    rxTotal: 0,
    expectedTotal: 0,
    lossPermilleSum: 0,
    maxLossPermille: 0,
    rateHzSum: 0,
    rateSamples: 0,
    channelSwitches: 0,
    hopStarts: 0,
    hopFinishes: 0,
    errorEvents: 0,
    maxSilentMs: 0,
  };
}

function printSummary(summary: Summary, json: boolean): void {
  const avgLossPermille = summary.samples > 0 ? summary.lossPermilleSum / summary.samples : 0;
  const avgRateHz = summary.rateSamples > 0 ? summary.rateHzSum / summary.rateSamples : 0;
  const totalLossPermille =
    summary.expectedTotal > 0
      ? Math.max(0, Math.min(1000, ((summary.expectedTotal - summary.rxTotal) * 1000) / summary.expectedTotal))
      : 0;
  const output = {
    type: "summary",
    samples: summary.samples,
    scoreFrames: summary.scores,
    rxTotal: summary.rxTotal,
    expectedTotal: summary.expectedTotal,
    totalLossPercent: Number((totalLossPermille / 10).toFixed(2)),
    averageLossPercent: Number((avgLossPermille / 10).toFixed(2)),
    maxLossPercent: Number((summary.maxLossPermille / 10).toFixed(2)),
    averageRateHz: Math.round(avgRateHz),
    channelSwitches: summary.channelSwitches,
    hopStarts: summary.hopStarts,
    hopFinishes: summary.hopFinishes,
    errorEvents: summary.errorEvents,
    maxSilentMs: summary.maxSilentMs,
    lastState: summary.lastState,
    lastChannel: summary.lastChannel,
    lastTargetChannel: summary.lastTargetChannel,
    latestScores: summary.latestScores,
  };

  if (json) {
    console.log(JSON.stringify(output));
    return;
  }

  console.log(
    `SUMMARY samples=${output.samples} scores=${output.scoreFrames} rx=${output.rxTotal}/${output.expectedTotal} totalLoss=${output.totalLossPercent}% avgLoss=${output.averageLossPercent}% maxLoss=${output.maxLossPercent}% avgRate=${output.averageRateHz}Hz switches=${output.channelSwitches} hop=${output.hopStarts}/${output.hopFinishes} err=${output.errorEvents} maxSilent=${output.maxSilentMs}ms last=${output.lastState ?? "-"}/${output.lastChannel ?? "-"}->${output.lastTargetChannel ?? "-"}`,
  );
  if (output.latestScores && output.latestScores.length > 0) {
    console.log(`SCORES ${output.latestScores.map((entry) => `${entry.channel}:${entry.score}`).join(" ")}`);
  }
}

async function main(): Promise<number> {
  const options = parseArgs(process.argv.slice(2));
  let HID: any;
  try {
    HID = require("node-hid");
  } catch (err) {
    console.error(`node-hid is not available: ${err instanceof Error ? err.message : String(err)}`);
    return 2;
  }

  const devices: HidDeviceInfo[] = HID.devices();
  if (options.list) {
    for (const device of devices) {
      console.log(JSON.stringify(device));
    }
    return 0;
  }

  const targets = devices.filter((device) => matchesTarget(device, options));
  if (targets.length === 0) {
    console.error("No RF HID telemetry interface found. Use --list, --vid, or --pid to inspect/select devices.");
    return 2;
  }

  const summary = buildSummary();
  const handles: any[] = [];
  let controlSeq = 1;

  const closeAll = () => {
    for (const handle of handles) {
      try {
        handle.close();
      } catch (_err) {
        // ignore close failures
      }
    }
  };

  for (const device of targets) {
    try {
      const handle = device.path ? new HID.HID(device.path) : new HID.HID(device.vendorId, device.productId);
      handle.on("data", (buf: Uint8Array) => {
        const events = parseDongleHidTelemetryFrame(buf, Date.now());
        for (const event of events) {
          if (event.kind !== "packet") continue;
          if (!event.messageType.startsWith("RFH_RHM1_") && event.messageType !== "RFH_RHS1_SCORE") continue;
          updateSummary(summary, event);
          if (options.json) {
            console.log(JSON.stringify({ type: "packet", ...event }));
          } else {
            console.log(packetLine(event));
          }
        }
      });
      handle.on("error", (err: unknown) => {
        if (options.json) {
          console.log(JSON.stringify({ type: "error", message: err instanceof Error ? err.message : String(err) }));
        } else {
          console.error(`HID error: ${err instanceof Error ? err.message : String(err)}`);
        }
      });
      handles.push(handle);

      if (options.configure) {
        const frame = buildControlFrame(
          {
            hidTelemetryEnabled: true,
            hidPeriodMs: options.periodMs,
            autoHopEnabled: options.manualChannel === null,
            manualChannel: options.manualChannel,
          },
          controlSeq,
        );
        controlSeq = controlSeq === 255 ? 1 : controlSeq + 1;
        writeControlFrame(handle, frame);
      }
    } catch (err) {
      if (!options.json) {
        console.error(`Open HID failed: ${err instanceof Error ? err.message : String(err)}`);
      }
    }
  }

  if (handles.length === 0) {
    return 2;
  }

  if (!options.json) {
    console.log(`Collecting RF HID quality for ${options.durationMs}ms from ${handles.length} interface(s)...`);
  }

  await new Promise<void>((resolve) => setTimeout(resolve, options.durationMs));
  closeAll();
  printSummary(summary, options.json);
  return summary.samples > 0 || summary.scores > 0 ? 0 : 1;
}

void main().then((code) => {
  process.exitCode = code;
});
