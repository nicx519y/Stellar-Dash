import type {
  ButtonLatencyEvent,
  ButtonLatencyStatusEvent,
  ErrorEvent,
  LatencyEvent,
  MonitorEvent,
  PacketEvent,
} from "../../../shared/monitor-types";

export type PacketRow = PacketEvent & { id: string };
export type ErrorRow = ErrorEvent & { id: string };
export type RatePoint = { tMs: number; hz: number };
export type LossPoint = { tMs: number; value: number };

export type ChannelScoreRow = {
  channel: number;
  score: number;
  rank: number;
  active: boolean;
  updatedAtMs: number;
};

export type ChannelSwitchRow = {
  id: string;
  timestampMs: number;
  type: "current" | "channel_change" | "hop_start" | "hop_finish" | "target_change" | "link_lost" | "link_recovered";
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

export type PacketSummary = {
  items: PacketRow[];
  usbTxPerSec: number;
  rfRxPerSec: number;
};

export type ErrorSummary = {
  items: ErrorRow[];
  windowSec: number;
  count: number;
};

export type LatencySummary = {
  estimatedHz: number;
  lastSeq: number;
  lastAtMs: number;
};

export type ButtonLatencySummary = {
  items: ButtonLatencyEvent[];
  status: ButtonLatencyStatusEvent | null;
};

export type MonitorStreamSnapshot = {
  events: MonitorEvent[];
  packets: PacketSummary;
  errors: ErrorSummary;
  latency: LatencySummary;
  buttonLatency: ButtonLatencySummary;
  rateSeries: RatePoint[];
  lossSeries: LossPoint[];
  channelSwitches: ChannelSwitchRow[];
  chart: {
    rateSeries: RatePoint[];
    lossSeries: LossPoint[];
    channelSwitches: ChannelSwitchRow[];
  };
  channelScores: ChannelScoreRow[];
};

export type MonitorStreamWorkerRequest =
  | { type: "batch"; events: MonitorEvent[] }
  | { type: "prependEvents"; events: MonitorEvent[] }
  | { type: "reset" }
  | { type: "flush" };

export type MonitorStreamWorkerResponse = {
  type: "snapshot";
  snapshot: MonitorStreamSnapshot;
};

export function createEmptyMonitorStreamSnapshot(): MonitorStreamSnapshot {
  return {
    events: [],
    packets: {
      items: [],
      usbTxPerSec: 0,
      rfRxPerSec: 0,
    },
    errors: {
      items: [],
      windowSec: 30,
      count: 0,
    },
    latency: {
      estimatedHz: 0,
      lastSeq: 0,
      lastAtMs: 0,
    },
    buttonLatency: {
      items: [],
      status: null,
    },
    rateSeries: [],
    lossSeries: [],
    channelSwitches: [],
    chart: {
      rateSeries: [],
      lossSeries: [],
      channelSwitches: [],
    },
    channelScores: [],
  };
}
