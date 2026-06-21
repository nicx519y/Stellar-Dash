import type {
  ButtonLatencyEvent,
  ButtonLatencyStatusEvent,
  ErrorEvent,
  LatencyEvent,
  MonitorEvent,
  PacketEvent,
} from "../../../shared/monitor-types";
import type {
  ChannelScoreRow,
  ChannelSwitchRow,
  ErrorRow,
  LossPoint,
  MonitorStreamSnapshot,
  PacketRow,
  RatePoint,
} from "./monitorStreamTypes";
import { createEmptyMonitorStreamSnapshot } from "./monitorStreamTypes";

type HopSession = {
  startedAtMs: number;
  from?: number;
  target?: number;
  scorePermille?: number;
  badScorePermille?: number;
  reason: string;
};

type LinkLossSession = {
  startedAtMs: number;
  from?: number;
  reason: string;
};

const MAX_ROWS = 500;
const MAX_INPUT_PACKET_ROWS = 500;
const MAX_EVENTS = 500;
const MAX_LATENCIES = 500;
const MAX_BUTTON_LATENCIES = 300;
const MAX_RATE_POINTS = 500;
const MAX_LOSS_POINTS = 600;
const MAX_CHANNEL_ROWS = 500;
const MAX_CHART_RATE_POINTS = 500;
const MAX_CHART_LOSS_POINTS = 500;
const MAX_CHART_CHANNEL_ROWS = 300;

function nowMs() {
  return Date.now();
}

function appendTrim<T>(current: T[], add: T[], max: number): T[] {
  if (add.length === 0) return current;
  const next = current.concat(add);
  return next.length > max ? next.slice(-max) : next;
}

function isPacket(ev: MonitorEvent): ev is PacketEvent {
  return ev.kind === "packet";
}

function isError(ev: MonitorEvent): ev is ErrorEvent {
  return ev.kind === "error";
}

function isLatency(ev: MonitorEvent): ev is LatencyEvent {
  return ev.kind === "latency";
}

function isButtonLatency(ev: MonitorEvent): ev is ButtonLatencyEvent {
  return ev.kind === "button_latency";
}

function isButtonLatencyStatus(ev: MonitorEvent): ev is ButtonLatencyStatusEvent {
  return ev.kind === "button_latency_status";
}

function calcRateFromPackets(packets: PacketRow[], channel: "USB" | "RF", direction: "TX" | "RX", windowMs: number) {
  const t = nowMs();
  const measuredWindowMs = Math.max(windowMs, 6500);
  for (let i = packets.length - 1; i >= 0; i--) {
    const p = packets[i];
    if (t - p.timestampMs > measuredWindowMs) break;
    if (p.channel === channel && p.direction === direction && typeof p.rateHz === "number") {
      return p.rateHz;
    }
  }

  let count = 0;
  for (let i = packets.length - 1; i >= 0; i--) {
    const p = packets[i];
    if (t - p.timestampMs > windowMs) break;
    if (p.channel === channel && p.direction === direction) count += 1;
  }
  return (count * 1000) / windowMs;
}

function calcErrorCount(errors: ErrorRow[], windowMs: number) {
  const t = nowMs();
  let count = 0;
  for (let i = errors.length - 1; i >= 0; i--) {
    const e = errors[i];
    if (t - e.timestampMs > windowMs) break;
    if (e.level !== "INFO") count += 1;
  }
  return count;
}

function calcHzFromLatency(latencies: LatencyEvent[]) {
  let last: LatencyEvent | null = null;
  for (let i = latencies.length - 1; i >= 0; i--) {
    const ev = latencies[i];
    if (typeof ev.seq === "number" && ev.seq > 0) {
      last = ev;
      break;
    }
  }
  if (!last) return { estimatedHz: 0, lastSeq: 0, lastAtMs: 0 };

  const lastAtMs = last.timestampMs;
  const lastSeq = last.seq;
  let prev: LatencyEvent | null = null;
  for (let i = latencies.length - 2; i >= 0; i--) {
    const ev = latencies[i];
    if (typeof ev.seq === "number" && ev.seq > 0 && ev.timestampMs !== lastAtMs) {
      prev = ev;
      break;
    }
  }
  if (!prev) return { estimatedHz: 0, lastSeq, lastAtMs };

  const dt = lastAtMs - prev.timestampMs;
  const ds = lastSeq - prev.seq;
  if (dt <= 0 || ds <= 0) return { estimatedHz: 0, lastSeq, lastAtMs };
  return { estimatedHz: (ds * 1000) / dt, lastSeq, lastAtMs };
}

function describeChannelReason(prev: PacketEvent | null, curr: PacketEvent) {
  const state = curr.rfStateCode ?? "";
  const prevState = prev?.rfStateCode ?? "";
  const loss = curr.lossPermille ?? prev?.lossPermille ?? 0;
  if (
    (state === "HR" || state === "CA" || state === "D" || prevState === "HR" || prevState === "CA" || prevState === "D") &&
    curr.targetChannelNumber === curr.channelNumber
  ) {
    return "Scheduled hop reached target channel";
  }
  if ((curr.unconnectedEvents ?? 0) > 0 || state === "U" || state === "PA" || prevState === "U" || prevState === "PA") {
    return "Channel changed after disconnect/reconnect";
  }
  if (loss > 30) {
    return "Channel changed after high packet loss";
  }
  return "Channel changed";
}

function qualityScoreFromBadScore(badScorePermille?: number) {
  if (typeof badScorePermille !== "number") return undefined;
  return Math.max(0, Math.min(1000, 1000 - badScorePermille));
}

function describeHopReason(badScorePermille?: number) {
  if (typeof badScorePermille !== "number") return "Unknown";
  if (badScorePermille >= 1000) return "Max bad score";
  return "Low quality score";
}

function rfPacketEvents(batch: MonitorEvent[]) {
  return batch.filter(isPacket).filter((p) => p.channel === "RF" && p.direction === "RX");
}

function isRfInputPacket(packet: PacketEvent) {
  return packet.messageType.startsWith("RFH_RHI1_") && packet.rfStateCode === "C";
}

function isHopIntent(packet: PacketEvent) {
  return (
    (packet.rfStateCode === "HR" || packet.rfStateCode === "CA" || packet.rfStateCode === "D") &&
    typeof packet.oldChannelNumber === "number" &&
    typeof packet.targetChannelNumber === "number" &&
    packet.oldChannelNumber !== packet.targetChannelNumber
  );
}

function isHopActivePacket(packet: PacketEvent) {
  return (
    isHopIntent(packet) ||
    packet.rfStateCode === "HR" ||
    packet.rfStateCode === "CA" ||
    packet.rfStateCode === "D" ||
    packet.rfStateCode === "RP" ||
    packet.rfStateCode === "RC"
  );
}

export class MonitorStreamProcessor {
  private events: MonitorEvent[] = [];
  private packetRows: PacketRow[] = [];
  private inputPacketRows: PacketRow[] = [];
  private errorRows: ErrorRow[] = [];
  private latencies: LatencyEvent[] = [];
  private buttonLatencies: ButtonLatencyEvent[] = [];
  private buttonLatencyStatus: ButtonLatencyStatusEvent | null = null;
  private rateSeries: RatePoint[] = [];
  private lossSeries: LossPoint[] = [];
  private channelSwitches: ChannelSwitchRow[] = [];
  private chartRateSeries: RatePoint[] = [];
  private chartLossSeries: LossPoint[] = [];
  private chartChannelSwitches: ChannelSwitchRow[] = [];
  private channelScores: ChannelScoreRow[] = [];
  private lastRfPacket: PacketEvent | null = null;
  private hopSession: HopSession | null = null;
  private linkLossSession: LinkLossSession | null = null;
  private nextId = 0;

  processBatch(batch: MonitorEvent[]): void {
    if (batch.length === 0) return;

    this.events = appendTrim(this.events, batch, MAX_EVENTS);
    this.packetRows = appendTrim(
      this.packetRows,
      batch.filter(isPacket).map((p) => ({ ...p, id: this.formatId("pkt", p.timestampMs) })),
      MAX_ROWS,
    );
    this.inputPacketRows = appendTrim(
      this.inputPacketRows,
      batch
        .filter(isPacket)
        .filter(isRfInputPacket)
        .map((p) => ({ ...p, id: this.formatId("input-pkt", p.timestampMs) })),
      MAX_INPUT_PACKET_ROWS,
    );
    this.errorRows = appendTrim(
      this.errorRows,
      batch
        .filter(isError)
        .filter((e) => e.level !== "INFO")
        .map((e) => ({ ...e, id: this.formatId("err", e.timestampMs) })),
      MAX_ROWS,
    );
    this.latencies = appendTrim(this.latencies, batch.filter(isLatency), MAX_LATENCIES);
    this.buttonLatencies = appendTrim(this.buttonLatencies, batch.filter(isButtonLatency), MAX_BUTTON_LATENCIES);

    const latestButtonLatencyStatus = batch.filter(isButtonLatencyStatus).at(-1);
    if (latestButtonLatencyStatus) {
      this.buttonLatencyStatus = latestButtonLatencyStatus;
    }

    const rfPackets = rfPacketEvents(batch);
    this.updateChannelScores(rfPackets);

    const trendPackets = rfPackets.filter(
      (p) => typeof p.rateHz === "number" && typeof p.lossPermille === "number",
    );
    const ratePoints = trendPackets.map((p) => ({ tMs: p.timestampMs, hz: p.rateHz ?? 0 }));
    const lossPoints = trendPackets.map((p) => ({ tMs: p.timestampMs, value: (p.lossPermille ?? 0) / 10 }));
    this.rateSeries = appendTrim(this.rateSeries, ratePoints, MAX_RATE_POINTS);
    this.lossSeries = appendTrim(this.lossSeries, lossPoints, MAX_LOSS_POINTS);
    this.chartRateSeries = appendTrim(this.chartRateSeries, ratePoints, MAX_CHART_RATE_POINTS);
    this.chartLossSeries = appendTrim(this.chartLossSeries, lossPoints, MAX_CHART_LOSS_POINTS);

    const channelRows = this.buildChannelRows(rfPackets);
    this.channelSwitches = appendTrim(this.channelSwitches, channelRows, MAX_CHANNEL_ROWS);
    this.chartChannelSwitches = appendTrim(this.chartChannelSwitches, channelRows, MAX_CHART_CHANNEL_ROWS);
  }

  prependEvents(older: MonitorEvent[]): void {
    if (older.length === 0) return;
    const next = older.concat(this.events);
    this.events = next.length > MAX_EVENTS ? next.slice(0, MAX_EVENTS) : next;
  }

  clear(): void {
    const empty = createEmptyMonitorStreamSnapshot();
    this.events = empty.events;
    this.packetRows = [];
    this.inputPacketRows = empty.packets.items;
    this.errorRows = empty.errors.items;
    this.latencies = [];
    this.buttonLatencies = empty.buttonLatency.items;
    this.buttonLatencyStatus = null;
    this.rateSeries = empty.rateSeries;
    this.lossSeries = empty.lossSeries;
    this.channelSwitches = empty.channelSwitches;
    this.chartRateSeries = empty.chart.rateSeries;
    this.chartLossSeries = empty.chart.lossSeries;
    this.chartChannelSwitches = empty.chart.channelSwitches;
    this.channelScores = empty.channelScores;
    this.lastRfPacket = null;
    this.hopSession = null;
    this.linkLossSession = null;
    this.nextId = 0;
  }

  snapshot(): MonitorStreamSnapshot {
    const windowSec = 30;
    return {
      events: this.events,
      packets: {
        items: this.inputPacketRows,
        usbTxPerSec: calcRateFromPackets(this.packetRows, "USB", "TX", 1000),
        rfRxPerSec: calcRateFromPackets(this.packetRows, "RF", "RX", 1000),
      },
      errors: {
        items: this.errorRows,
        windowSec,
        count: calcErrorCount(this.errorRows, windowSec * 1000),
      },
      latency: calcHzFromLatency(this.latencies),
      buttonLatency: {
        items: this.buttonLatencies,
        status: this.buttonLatencyStatus,
      },
      rateSeries: this.rateSeries,
      lossSeries: this.lossSeries,
      channelSwitches: this.channelSwitches,
      chart: {
        rateSeries: this.chartRateSeries,
        lossSeries: this.chartLossSeries,
        channelSwitches: this.chartChannelSwitches,
      },
      channelScores: this.channelScores,
    };
  }

  private formatId(prefix: string, timestampMs = nowMs()) {
    this.nextId += 1;
    return `${prefix}-${timestampMs}-${this.nextId}`;
  }

  private updateChannelScores(rfPackets: PacketEvent[]): void {
    let scorePacket: PacketEvent | undefined;
    for (let i = rfPackets.length - 1; i >= 0; i--) {
      const packet = rfPackets[i];
      if (Array.isArray(packet.channelScores) && packet.channelScores.length > 0) {
        scorePacket = packet;
        break;
      }
    }
    if (!scorePacket?.channelScores) return;

    const activeChannel = scorePacket.channelNumber;
    this.channelScores = scorePacket.channelScores
      .map((entry) => ({
        channel: entry.channel,
        score: Math.max(0, Math.min(1000, entry.score)),
      }))
      .sort((a, b) => a.score - b.score || a.channel - b.channel)
      .map((entry, index) => ({
        channel: entry.channel,
        score: entry.score,
        rank: index + 1,
        active: entry.channel === activeChannel,
        updatedAtMs: scorePacket.timestampMs,
      }));
  }

  private buildChannelRows(rfPackets: PacketEvent[]): ChannelSwitchRow[] {
    const channelRows: ChannelSwitchRow[] = [];
    for (const p of rfPackets) {
      if (p.messageType === "RFH_RHS1_SCORE") {
        continue;
      }
      if (isRfInputPacket(p)) {
        continue;
      }
      const prev = this.lastRfPacket;
      const hopActive = isHopActivePacket(p);
      const hopSession = this.hopSession;
      const state = p.rfStateCode ?? "";
      if (typeof p.channelNumber !== "number") {
        this.lastRfPacket = p;
        continue;
      }

      if (state === "U") {
        if (!this.linkLossSession) {
          this.linkLossSession = {
            startedAtMs: p.timestampMs,
            from: prev?.channelNumber,
            reason: "No DATA timeout",
          };
          channelRows.push({
            id: this.formatId("link-lost", p.timestampMs),
            timestampMs: p.timestampMs,
            type: "link_lost",
            from: prev?.channelNumber,
            to: p.channelNumber,
            target: p.targetChannelNumber,
            state: p.rfStateCode,
            reason: "No DATA timeout",
            lossPercent: typeof p.lossPermille === "number" ? p.lossPermille / 10 : undefined,
            durationMs: p.maxSilentMs,
            rateHz: p.rateHz,
          });
        }
        this.lastRfPacket = p;
        continue;
      }

      if (this.linkLossSession && state === "C") {
        const session = this.linkLossSession;
        channelRows.push({
          id: this.formatId("link-recovered", p.timestampMs),
          timestampMs: p.timestampMs,
          type: "link_recovered",
          from: session.from ?? prev?.channelNumber,
          to: p.channelNumber,
          target: p.targetChannelNumber,
          state: p.rfStateCode,
          reason: "DATA received again",
          lossPercent: typeof p.lossPermille === "number" ? p.lossPermille / 10 : undefined,
          durationMs: Math.max(0, p.timestampMs - session.startedAtMs),
          rateHz: p.rateHz,
        });
        this.linkLossSession = null;
        this.lastRfPacket = p;
        continue;
      }

      if (p.hopEvent === "start") {
        const badScorePermille = p.hopScorePermille ?? p.hopEventValue ?? p.lossPermille;
        const scorePermille = qualityScoreFromBadScore(badScorePermille);
        const reason = describeHopReason(badScorePermille);
        this.hopSession = {
          startedAtMs: p.timestampMs,
          from: p.oldChannelNumber ?? p.channelNumber,
          target: p.targetChannelNumber,
          scorePermille,
          badScorePermille,
          reason,
        };
        channelRows.push({
          id: this.formatId("hop-start", p.timestampMs),
          timestampMs: p.timestampMs,
          type: "hop_start",
          from: p.oldChannelNumber ?? p.channelNumber,
          to: p.targetChannelNumber,
          target: p.targetChannelNumber,
          state: p.rfStateCode,
          reason,
          lossPercent: typeof badScorePermille === "number" ? badScorePermille / 10 : undefined,
          scorePermille,
          badScorePermille,
          rateHz: p.rateHz,
        });
        this.lastRfPacket = p;
        continue;
      }

      if (p.hopEvent === "finish") {
        const session = this.hopSession;
        const durationMs =
          p.hopDurationMs ??
          p.hopEventValue ??
          (session ? Math.max(0, p.timestampMs - session.startedAtMs) : undefined);
        channelRows.push({
          id: this.formatId("hop-end", p.timestampMs),
          timestampMs: p.timestampMs,
          type: "hop_finish",
          from: session?.from ?? p.oldChannelNumber,
          to: p.channelNumber,
          target: session?.target ?? p.targetChannelNumber,
          state: p.rfStateCode,
          reason: session?.reason ?? "Completed",
          lossPercent: typeof p.lossPermille === "number" ? p.lossPermille / 10 : undefined,
          scorePermille: session?.scorePermille,
          badScorePermille: session?.badScorePermille,
          durationMs,
          rateHz: p.rateHz,
        });
        this.hopSession = null;
        this.lastRfPacket = p;
        continue;
      }

      if (hopActive && !hopSession) {
        const badScorePermille = p.lossPermille;
        const scorePermille = qualityScoreFromBadScore(badScorePermille);
        const reason = describeHopReason(badScorePermille);
        this.hopSession = {
          startedAtMs: p.timestampMs,
          from: p.oldChannelNumber ?? p.channelNumber,
          target: p.targetChannelNumber,
          scorePermille,
          badScorePermille,
          reason,
        };
        channelRows.push({
          id: this.formatId("hop-start", p.timestampMs),
          timestampMs: p.timestampMs,
          type: "hop_start",
          from: p.oldChannelNumber ?? p.channelNumber,
          to: p.targetChannelNumber,
          target: p.targetChannelNumber,
          state: p.rfStateCode,
          reason,
          lossPercent: typeof badScorePermille === "number" ? badScorePermille / 10 : undefined,
          scorePermille,
          badScorePermille,
          rateHz: p.rateHz,
        });
        this.lastRfPacket = p;
        continue;
      }

      if (!hopActive && hopSession) {
        const durationMs = Math.max(0, p.timestampMs - hopSession.startedAtMs);
        channelRows.push({
          id: this.formatId("hop-end", p.timestampMs),
          timestampMs: p.timestampMs,
          type: "hop_finish",
          from: hopSession.from,
          to: p.channelNumber,
          target: hopSession.target ?? p.targetChannelNumber,
          state: p.rfStateCode,
          reason: hopSession.reason,
          lossPercent: typeof p.lossPermille === "number" ? p.lossPermille / 10 : undefined,
          scorePermille: hopSession.scorePermille,
          badScorePermille: hopSession.badScorePermille,
          durationMs,
          rateHz: p.rateHz,
        });
        this.hopSession = null;
        this.lastRfPacket = p;
        continue;
      }

      if (hopActive) {
        this.lastRfPacket = p;
        continue;
      }

      if (!prev || typeof prev.channelNumber !== "number") {
        channelRows.push({
          id: this.formatId("ch", p.timestampMs),
          timestampMs: p.timestampMs,
          type: "current",
          from: p.oldChannelNumber,
          to: p.channelNumber,
          target: p.targetChannelNumber,
          state: p.rfStateCode,
          reason: "Current channel",
          lossPercent: typeof p.lossPermille === "number" ? p.lossPermille / 10 : undefined,
          rateHz: p.rateHz,
        });
      } else if (p.channelNumber !== prev.channelNumber) {
        channelRows.push({
          id: this.formatId("ch", p.timestampMs),
          timestampMs: p.timestampMs,
          type: "channel_change",
          from: prev.channelNumber,
          to: p.channelNumber,
          target: p.targetChannelNumber,
          state: p.rfStateCode,
          reason: describeChannelReason(prev, p),
          lossPercent: typeof p.lossPermille === "number" ? p.lossPermille / 10 : undefined,
          rateHz: p.rateHz,
        });
      } else if (
        isHopIntent(p) &&
        (!isHopIntent(prev) ||
          prev.oldChannelNumber !== p.oldChannelNumber ||
          prev.targetChannelNumber !== p.targetChannelNumber ||
          prev.rfStateCode !== p.rfStateCode)
      ) {
        channelRows.push({
          id: this.formatId("hop", p.timestampMs),
          timestampMs: p.timestampMs,
          type: "hop_start",
          from: p.oldChannelNumber,
          to: p.targetChannelNumber,
          target: p.targetChannelNumber,
          state: p.rfStateCode,
          reason: describeHopReason(p.lossPermille),
          lossPercent: typeof p.lossPermille === "number" ? p.lossPermille / 10 : undefined,
          scorePermille: qualityScoreFromBadScore(p.lossPermille),
          badScorePermille: p.lossPermille,
          rateHz: p.rateHz,
        });
      } else if (
        typeof p.targetChannelNumber === "number" &&
        p.targetChannelNumber !== p.channelNumber &&
        p.targetChannelNumber !== prev.targetChannelNumber
      ) {
        channelRows.push({
          id: this.formatId("target", p.timestampMs),
          timestampMs: p.timestampMs,
          type: "target_change",
          from: p.channelNumber,
          to: p.targetChannelNumber,
          target: p.targetChannelNumber,
          state: p.rfStateCode,
          reason: "Target channel changed",
          lossPercent: typeof p.lossPermille === "number" ? p.lossPermille / 10 : undefined,
          rateHz: p.rateHz,
        });
      }
      this.lastRfPacket = p;
    }
    return channelRows;
  }
}
