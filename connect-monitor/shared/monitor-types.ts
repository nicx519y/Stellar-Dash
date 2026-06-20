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
  inputKeyMask?: number;
  inputSeq?: number;
  inputFlags?: number;
  airRateCode?: number;
  airLastDataSeq?: number;
  airLinkActive?: boolean;
  airPendingDrop?: number;
  airPendingCurrent?: number;
  airPendingMax?: number;
  airWindowRxOk?: number;
  airWindowExpected?: number;
  airWindowErrors?: number;
  airWindowCrcErrors?: number;
  airWindowSeqGaps?: number;
  airWindowTypeErrors?: number;
  airWindowTimeoutErrors?: number;
  hostMonoUs?: number;
  sampleTickUs?: number;
  syncSeq?: number;
  syncRxTickUs?: number;
  syncTxTickUs?: number;
}

export interface LatencyEvent {
  kind: "latency";
  timestampMs: number;
  seq: number;
  deviceToUsbSubmitUs?: number;
  deviceToRfUs?: number;
  rfToUsbSubmitUs?: number;
}

export interface ButtonLatencyEvent {
  kind: "button_latency";
  timestampMs: number;
  inputSeq: number;
  keyMask: number;
  standardMask: number;
  previousStandardMask: number;
  action: "press" | "release" | "change";
  latencyMs: number;
  sampleTickUs: number;
  samplePcUs: number;
  xinputPcUs: number;
  syncRttUs?: number;
  confidence: "high" | "medium" | "low";
}

export interface ButtonLatencyStatusEvent {
  kind: "button_latency_status";
  timestampMs: number;
  status: "Syncing" | "No HID telemetry" | "No XInput" | "No match" | "Locked";
  syncRttUs?: number;
  clockSamples?: number;
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

export type MonitorEvent = DeviceStatusEvent | PacketEvent | LatencyEvent | ButtonLatencyEvent | ButtonLatencyStatusEvent | ErrorEvent;

export interface SerialPortInfo {
  path: string;
  displayName: string;
  manufacturer?: string;
  friendlyName?: string;
  vendorId?: string;
  productId?: string;
}

export interface SerialLogLine {
  id: string;
  timestampMs: number;
  portPath: string;
  text: string;
}

export type DebugApplyState = "Idle" | "Applying" | "Applied" | "Partial" | "Failed";
export type DebugHidPeriodMs = 100 | 250 | 500 | 1000;

export interface DebugConfig {
  hidTelemetryEnabled: boolean;
  hidPeriodMs: DebugHidPeriodMs;
  rxLogEnabled: boolean;
  txLogEnabled: boolean;
  stm32LogEnabled: boolean;
  autoHopEnabled: boolean;
  manualChannel: number | null;
}

export interface DebugConfigStatus {
  state: DebugApplyState;
  rxStatus: DebugApplyState;
  txStatus: DebugApplyState;
  stm32Status: DebugApplyState;
  lastSeq: number;
  message?: string;
}
