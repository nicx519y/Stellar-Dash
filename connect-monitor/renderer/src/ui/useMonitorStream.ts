import * as React from "react";

import type { MonitorEvent, PacketEvent, ErrorEvent, LatencyEvent } from "../../../shared/monitor-types";

type PacketRow = PacketEvent & { id: string };
type ErrorRow = ErrorEvent & { id: string };
type RatePoint = { tMs: number; hz: number };
type LossPoint = { tMs: number; value: number };
export type ChannelSwitchRow = {
  id: string;
  timestampMs: number;
  type: "current" | "channel_change" | "hop_start" | "hop_finish" | "target_change";
  from?: number;
  to?: number;
  target?: number;
  state?: string;
  reason: string;
  lossPercent?: number;
  scorePermille?: number;
  badScorePermille?: number;
  durationMs?: number;
  rateHz?: number;
};

type HopSession = {
  startedAtMs: number;
  from?: number;
  target?: number;
  scorePermille?: number;
  badScorePermille?: number;
  reason: string;
};

const MAX_ROWS = 500;
const MAX_EVENTS = 500;
const MAX_LATENCIES = 500;
const MAX_RATE_POINTS = 600;
const MAX_LOSS_POINTS = 600;
const MAX_CHANNEL_ROWS = 500;

function nowMs() {
  return Date.now();
}

function formatId(prefix: string) {
  return `${prefix}-${Math.random().toString(16).slice(2)}-${Date.now()}`;
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
    if (p.channel === channel && p.direction === direction) count++;
  }
  return (count * 1000) / windowMs;
}

function calcErrorCount(errors: ErrorRow[], windowMs: number) {
  const t = nowMs();
  let count = 0;
  for (let i = errors.length - 1; i >= 0; i--) {
    const e = errors[i];
    if (t - e.timestampMs > windowMs) break;
    if (e.level !== "INFO") count++;
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
  if (badScorePermille >= 1000) return "ACK missed";
  return "Low quality score";
}

function rfPacketEvents(batch: MonitorEvent[]) {
  return batch.filter(isPacket).filter((p) => p.channel === "RF" && p.direction === "RX");
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

export function useMonitorStream() {
  const [events, setEvents] = React.useState<MonitorEvent[]>([]);
  const [packetRows, setPacketRows] = React.useState<PacketRow[]>([]);
  const [errorRows, setErrorRows] = React.useState<ErrorRow[]>([]);
  const [latencies, setLatencies] = React.useState<LatencyEvent[]>([]);
  const [rateSeries, setRateSeries] = React.useState<RatePoint[]>([]);
  const [lossSeries, setLossSeries] = React.useState<LossPoint[]>([]);
  const [channelSwitches, setChannelSwitches] = React.useState<ChannelSwitchRow[]>([]);
  const [paused, setPausedState] = React.useState(false);
  const pausedRef = React.useRef(false);
  pausedRef.current = paused;
  const lastRfPacketRef = React.useRef<PacketEvent | null>(null);
  const hopSessionRef = React.useRef<HopSession | null>(null);

  React.useEffect(() => {
    let unsub: (() => void) | null = null;

    const handler = (batch: MonitorEvent[]) => {
      if (pausedRef.current) return;
      setEvents((prev) => {
        const next = prev.concat(batch);
        return next.length > MAX_EVENTS ? next.slice(-MAX_EVENTS) : next;
      });
      setPacketRows((prev) => {
        const add = batch.filter(isPacket).map((p) => ({ ...p, id: formatId("pkt") }));
        const next = prev.concat(add);
        return next.length > MAX_ROWS ? next.slice(-MAX_ROWS) : next;
      });
      setErrorRows((prev) => {
        const add = batch
          .filter(isError)
          .filter((e) => e.level !== "INFO")
          .map((e) => ({ ...e, id: formatId("err") }));
        const next = prev.concat(add);
        return next.length > MAX_ROWS ? next.slice(-MAX_ROWS) : next;
      });
      setLatencies((prev) => {
        const add = batch.filter(isLatency);
        const next = prev.concat(add);
        return next.length > MAX_LATENCIES ? next.slice(-MAX_LATENCIES) : next;
      });
      const rfPackets = rfPacketEvents(batch);
      const trendPackets = rfPackets.filter(
        (p) => typeof p.rateHz === "number" && typeof p.lossPermille === "number",
      );
      setRateSeries((prev) => {
        const add = trendPackets.map((p) => ({ tMs: p.timestampMs, hz: p.rateHz ?? 0 }));
        if (add.length === 0) return prev;
        const next = prev.concat(add);
        return next.length > MAX_RATE_POINTS ? next.slice(-MAX_RATE_POINTS) : next;
      });
      setLossSeries((prev) => {
        const add = trendPackets.map((p) => ({ tMs: p.timestampMs, value: (p.lossPermille ?? 0) / 10 }));
        if (add.length === 0) return prev;
        const next = prev.concat(add);
        return next.length > MAX_LOSS_POINTS ? next.slice(-MAX_LOSS_POINTS) : next;
      });
      const channelRows: ChannelSwitchRow[] = [];
      for (const p of rfPackets) {
        const prev = lastRfPacketRef.current;
        const hopActive = isHopActivePacket(p);
        const hopSession = hopSessionRef.current;
        if (typeof p.channelNumber !== "number") {
          lastRfPacketRef.current = p;
          continue;
        }

        if (p.hopEvent === "start") {
          const badScorePermille = p.hopScorePermille ?? p.hopEventValue ?? p.lossPermille;
          const scorePermille = qualityScoreFromBadScore(badScorePermille);
          const reason = describeHopReason(badScorePermille);
          hopSessionRef.current = {
            startedAtMs: p.timestampMs,
            from: p.oldChannelNumber ?? p.channelNumber,
            target: p.targetChannelNumber,
            scorePermille,
            badScorePermille,
            reason,
          };
          channelRows.push({
            id: formatId("hop-start"),
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
          lastRfPacketRef.current = p;
          continue;
        }

        if (p.hopEvent === "finish") {
          const session = hopSessionRef.current;
          const durationMs =
            p.hopDurationMs ??
            p.hopEventValue ??
            (session ? Math.max(0, p.timestampMs - session.startedAtMs) : undefined);
          channelRows.push({
            id: formatId("hop-end"),
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
          hopSessionRef.current = null;
          lastRfPacketRef.current = p;
          continue;
        }

        if (hopActive && !hopSession) {
          const badScorePermille = p.lossPermille;
          const scorePermille = qualityScoreFromBadScore(badScorePermille);
          const reason = describeHopReason(badScorePermille);
          hopSessionRef.current = {
            startedAtMs: p.timestampMs,
            from: p.oldChannelNumber ?? p.channelNumber,
            target: p.targetChannelNumber,
            scorePermille,
            badScorePermille,
            reason,
          };
          channelRows.push({
            id: formatId("hop-start"),
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
          lastRfPacketRef.current = p;
          continue;
        }

        if (!hopActive && hopSession) {
          const durationMs = Math.max(0, p.timestampMs - hopSession.startedAtMs);
          channelRows.push({
            id: formatId("hop-end"),
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
          hopSessionRef.current = null;
          lastRfPacketRef.current = p;
          continue;
        }

        if (hopActive) {
          lastRfPacketRef.current = p;
          continue;
        }

        if (!prev || typeof prev.channelNumber !== "number") {
          channelRows.push({
            id: formatId("ch"),
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
            id: formatId("ch"),
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
            id: formatId("hop"),
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
            id: formatId("target"),
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
        lastRfPacketRef.current = p;
      }
      if (channelRows.length > 0) {
        setChannelSwitches((prev) => {
          const next = prev.concat(channelRows);
          return next.length > MAX_CHANNEL_ROWS ? next.slice(-MAX_CHANNEL_ROWS) : next;
        });
      }
    };

    if (window.connectMonitorApi?.onEvents) {
      unsub = window.connectMonitorApi.onEvents(handler);
      window.connectMonitorApi.getSnapshot(200).then((snap) => handler(snap)).catch(() => {});
    }

    return () => {
      if (unsub) unsub();
    };
  }, []);

  const clear = React.useCallback(() => {
    setEvents([]);
    setPacketRows([]);
    setErrorRows([]);
    setLatencies([]);
    setRateSeries([]);
    setLossSeries([]);
    setChannelSwitches([]);
    lastRfPacketRef.current = null;
    hopSessionRef.current = null;
    if (window.connectMonitorApi?.clear) {
      window.connectMonitorApi.clear().catch(() => {});
    }
  }, []);

  const setPaused = React.useCallback(async (nextPaused: boolean) => {
    setPausedState(nextPaused);
    if (window.connectMonitorApi?.setPaused) {
      try {
        await window.connectMonitorApi.setPaused(nextPaused);
      } catch {
      }
    }
  }, []);

  const loadOlderEvents = React.useCallback(async () => {
    if (!window.connectMonitorApi?.queryEvents || events.length === 0) return;
    const before = events[0].timestampMs;
    const older = await window.connectMonitorApi.queryEvents(before, 500);
    if (older.length === 0) return;
    setEvents((prev) => {
      const next = older.concat(prev);
      return next.length > MAX_EVENTS ? next.slice(0, MAX_EVENTS) : next;
    });
  }, [events]);

  React.useEffect(() => {
    if (window.connectMonitorApi?.getPaused) {
      window.connectMonitorApi
        .getPaused()
        .then((p) => setPausedState(Boolean(p)))
        .catch(() => {});
    }
  }, []);

  const packets = React.useMemo(() => {
    return {
      items: packetRows,
      usbTxPerSec: calcRateFromPackets(packetRows, "USB", "TX", 1000),
      rfRxPerSec: calcRateFromPackets(packetRows, "RF", "RX", 1000),
    };
  }, [packetRows]);

  const errors = React.useMemo(() => {
    const windowSec = 30;
    return {
      items: errorRows,
      windowSec,
      count: calcErrorCount(errorRows, windowSec * 1000),
    };
  }, [errorRows]);

  const latency = React.useMemo(() => calcHzFromLatency(latencies), [latencies]);

  return { events, packets, errors, latency, rateSeries, lossSeries, channelSwitches, paused, setPaused, clear, loadOlderEvents };
}
