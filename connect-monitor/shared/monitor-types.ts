export type ConnectionMode = "USB" | "RF24G";
export type LinkState = "Disconnected" | "Connecting" | "Connected" | "Error";
export type ErrorLevel = "INFO" | "WARN" | "ERROR" | "FATAL";

export interface DeviceStatusEvent {
  kind: "device_status";
  timestampMs: number;
  mode: ConnectionMode;
  state: LinkState;
  statusLabel?: string;
  targetRateHz: number;
  actualRateHz: number;
}

export interface PacketEvent {
  kind: "packet";
  timestampMs: number;
  channel: "USB" | "RF";
  direction: "TX" | "RX";
  seq?: number;
  messageType: string;
  payloadLen: number;
  payloadHex?: string;
  sampleCount?: number;
  expectedCount?: number;
  sampleWindowMs?: number;
  rateHz?: number;
  lossPermille?: number;
  channelNumber?: number;
  rfStateCode?: string;
  oldChannelNumber?: number;
  targetChannelNumber?: number;
  hopEvent?: "start" | "finish";
  hopEventValue?: number;
  hopScorePermille?: number;
  hopDurationMs?: number;
  maxSilentTicks?: number;
  maxSilentMs?: number;
  unconnectedEvents?: number;
  errorEvents?: number;
  channelScores?: Array<{ channel: number; score: number }>;
  activeChannelScore?: number;
}

export interface LatencyEvent {
  kind: "latency";
  timestampMs: number;
  seq: number;
  deviceToUsbSubmitUs?: number;
  deviceToRfUs?: number;
  rfToUsbSubmitUs?: number;
}

export interface ErrorEvent {
  kind: "error";
  timestampMs: number;
  source: string;
  code: string;
  level: ErrorLevel;
  message: string;
  count?: number;
}

export type MonitorEvent = DeviceStatusEvent | PacketEvent | LatencyEvent | ErrorEvent;
