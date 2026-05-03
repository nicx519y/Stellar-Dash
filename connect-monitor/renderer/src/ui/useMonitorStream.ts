import * as React from "react";

import type { MonitorEvent, PacketEvent, ErrorEvent, LatencyEvent } from "../../../shared/monitor-types";

type PacketRow = PacketEvent & { id: string };
type ErrorRow = ErrorEvent & { id: string };
type RatePoint = { tMs: number; hz: number };

const MAX_ROWS = 100;
const MAX_EVENTS = 800;
const MAX_LATENCIES = 200;
const MAX_RATE_POINTS = 100;
const RATE_SAMPLE_INTERVAL_MS = 250;

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

export function useMonitorStream() {
  const [events, setEvents] = React.useState<MonitorEvent[]>([]);
  const [packetRows, setPacketRows] = React.useState<PacketRow[]>([]);
  const [errorRows, setErrorRows] = React.useState<ErrorRow[]>([]);
  const [latencies, setLatencies] = React.useState<LatencyEvent[]>([]);
  const [rateSeries, setRateSeries] = React.useState<RatePoint[]>([]);
  const [paused, setPausedState] = React.useState(false);
  const pausedRef = React.useRef(false);
  pausedRef.current = paused;
  const packetRowsRef = React.useRef<PacketRow[]>([]);
  packetRowsRef.current = packetRows;
  const latenciesRef = React.useRef<LatencyEvent[]>([]);
  latenciesRef.current = latencies;

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
    };

    if (window.connectMonitorApi?.onEvents) {
      unsub = window.connectMonitorApi.onEvents(handler);
      window.connectMonitorApi.getSnapshot(200).then((snap) => handler(snap)).catch(() => {});
    }

    return () => {
      if (unsub) unsub();
    };
  }, []);

  React.useEffect(() => {
    let timer: number | null = null;
    const tick = () => {
      if (pausedRef.current) return;
      const latencyHz = calcHzFromLatency(latenciesRef.current).estimatedHz || 0;
      const usbHz = calcRateFromPackets(packetRowsRef.current, "USB", "TX", 1000);
      const rfHz = calcRateFromPackets(packetRowsRef.current, "RF", "RX", 1000);
      const hz = latencyHz > 0 ? latencyHz : Math.max(usbHz, rfHz);
      setRateSeries((prev) => {
        const next = prev.concat({ tMs: nowMs(), hz });
        return next.length > MAX_RATE_POINTS ? next.slice(-MAX_RATE_POINTS) : next;
      });
    };
    timer = window.setInterval(tick, RATE_SAMPLE_INTERVAL_MS);
    return () => {
      if (timer != null) window.clearInterval(timer);
    };
  }, []);

  const clear = React.useCallback(() => {
    setEvents([]);
    setPacketRows([]);
    setErrorRows([]);
    setLatencies([]);
    setRateSeries([]);
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

  return { events, packets, errors, latency, rateSeries, paused, setPaused, clear };
}
