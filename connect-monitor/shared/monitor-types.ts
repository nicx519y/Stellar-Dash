import type { RxProfile } from "./rx-profile";
import type { RfTxMetrics } from "./rf-tx-metrics";
import type { FastStatus, FastEvent } from "./fast-recovery";
export type ConnectionMode = "USB" | "RF24G";
export type LinkState =
  | "Disconnected"
  | "Pairing"
  | "Connecting"
  | "Connected"
  | "Reconnecting"
  | "Error";
export type ErrorLevel = "INFO" | "WARN" | "ERROR" | "FATAL";
export type Ch585Role = "Unknown" | "RF" | "USB" | "Maintenance";

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
  rxProfile?: RxProfile;
  rfTxMetrics?: RfTxMetrics;
  rfProtocolVersion?: number;
  rfFast?: FastStatus;
  rfFastEvent?: FastEvent;
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
  targetRateHz?: number;
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
  rssiSamples?: number;
  rssiAvg?: number;
  rssiMin?: number;
  rssiMax?: number;
  rssiLast?: number;
  inputKeyMask?: number;
  inputSeq?: number;
  inputFlags?: number;
  airRateCode?: number;
  airLastDataSeq?: number;
  airLinkActive?: boolean;
  rfDiagnosticVersion?: number;
  rfBuildId?: number;
  rfDiagnosticPage?: number;
  rfChannel?: {
    page: number;
    ageMs: number;
    state?: number;
    primary?: number;
    candidate?: number;
    backups?: number[];
    probeDisabled?: number;
    reason?: number;
    maintenanceUs?: number;
    beforePermille?: number;
    afterPermille?: number;
    localCaps?: number;
    peerCaps?: number;
    switches?: number;
    probes?: number;
    failures?: number;
    probeFailures?: number;
    sampleCount?: number;
    probation?: boolean;
    sampleSource?: number;
    historyAgeMs?: number;
    beforeGapUs?: number;
    transitionGapUs?: number;
    reservationFailures?: number;
    radioFailures?: number;
    candidateFailures?: number;
    scores?: Array<{ channel: number; lossPermille: number }>;
    lastGapUs?: number;
    maxGapUs?: number;
    firstPacketUs?: number;
    transitionMissing?: number;
    inputCoalesced?: number;
    versionMismatches?: number;
    receiverState?: number;
    receiverChannel?: number;
  };
  rfReadyMs?: number;
  usbReadyMs?: number;
  rfConnectMs?: number;
  rfConnectCount?: number;
  rfAckWatchdog?: number;
  rfAckLate?: number;
  rfAckDuplicates?: number;
  rfInputEdgeDrop?: number;
  rfCrcTotal?: number;
  rfRxArmFailures?: number;
  rfRxRearmMaxUs?: number;
  rfRxCallbackMaxUs?: number;
  rfInputCommitMaxUs?: number;
  rfInputCaptureMaxUs?: number;
  rfAckSendFailures?: number;
  rfShortDecodedTotal?: number;
  rfTx5ByteTotal?: number;
  rfTx7ByteTotal?: number;
  rfTx12ByteTotal?: number;
  rfAckReservedSlots?: number;
  rfControlGuardSlots?: number;
  rfTraceOverwrites?: number;
  rfSourceReceived?: number;
  rfSourceMatched?: number;
  rfSourceExpired?: number;
  rfSourceQueueDrops?: number;
  rfSourceBoundaryMissing?: number;
  rfSourceSpiDrops?: number;
  rfSourceIdentityWaits?: number;
  rfTxDiagnosticValid?: boolean;
  rfTxWindowMs?: number;
  rfTxDue?: number;
  rfTxStarted?: number;
  rfTxDropped?: number;
  rfTxDiagnosticAgeMs?: number;
  rfAirMissingTotal?: number;
  rfAirReceivedTotal?: number;
  rfReceiverState?: number;
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
  latencyUs?: number;
  latencyStm32Us?: number;
  latencyTxUs?: number;
  latencyRxUs?: number;
  latencyRxIrqUs?: number;
  latencyRxDecodeUs?: number;
  latencyRxEpWaitUs?: number;
  latencyRxSubmitUs?: number;
  latencyStageFlags?: number;
  traceDrops?: number;
  traceBaseline?: boolean;
  syncSeq?: number;
  syncRxTickUs?: number;
  syncQueueWaitUs?: number;
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
  latencyMs: number | null;
  measurement?: "stages" | "windows" | "trace" | "usb";
  relativeStagesUs?: Array<number | null>;
  measurementReason?: string;
  traceId?: string;
  latencyMinMs?: number;
  latencyMaxMs?: number;
  latencyStageFlags?: number;
  pollIntervalUs?: number;
  stm32Ms?: number;
  txMs?: number;
  rxMs?: number;
  rxIrqMs?: number;
  rxDecodeMs?: number;
  rxEpWaitMs?: number;
  rxSubmitMs?: number;
  latencyFrame?: string;
  sampleTickUs: number;
  samplePcUs: number;
  xinputPcUs: number;
  syncRttUs?: number;
  confidence: "high" | "medium" | "low";
}

export interface ButtonLatencyStatusEvent {
  kind: "button_latency_status";
  timestampMs: number;
  status: "Syncing" | "No HID telemetry" | "No XInput" | "No match" | "Locked" | "Waiting edge" | "Live";
  syncRttUs?: number;
  clockSamples?: number;
  clockWidthUs?: number;
  syncRequestSeq?: number;
  syncPcSendUs?: number;
  syncPcReceiveUs?: number;
}

export interface PowerStatusEvent {
  kind: "power_status";
  timestampMs: number;
  h1Mv: number;
  h2Mv: number;
  batMv: number;
  socPercent: number;
  activeBattery: "H1" | "H2";
  chargeState: "Discharging" | "Charging" | "Full" | "Fault" | "Unknown";
  valid: boolean;
  lowBattery: boolean;
  /** Latest-PCB single-cell fields. Legacy V1 parsers populate cellMv too. */
  cellMv?: number;
  vbusMv?: number;
  chargeCurrentMa?: number;
  faultBits?: number;
  vbusPresent?: boolean;
  fastCharging?: boolean;
  gaugeOnline?: boolean;
  chargerOnline?: boolean;
  ch585Role?: Ch585Role;
  ch585Version?: string;
  formatVersion?: 1 | 2;
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

export type MonitorEvent =
  | DeviceStatusEvent
  | PacketEvent
  | LatencyEvent
  | ButtonLatencyEvent
  | ButtonLatencyStatusEvent
  | PowerStatusEvent
  | ErrorEvent;

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
  latencyMeasurementEnabled?: boolean;
  hidTelemetryEnabled: boolean;
  hidPeriodMs: DebugHidPeriodMs;
  autoHopEnabled: boolean;
  manualChannel: number | null;
}

export interface DebugConfigStatus {
  state: DebugApplyState;
  rxStatus: DebugApplyState;
  txStatus: DebugApplyState;
  lastSeq: number;
  message?: string;
  telemetryLeaseSupported?: boolean;
}

export interface HitboxBounds {
  x: number;
  y: number;
  width: number;
  height: number;
  visible: boolean;
  compact: boolean;
}

export interface LatencyTableBounds {
  x: number;
  y: number;
  width: number;
  height: number;
  visible: boolean;
}

export interface HitboxOptions {
  compact: boolean;
}

export interface HitboxSummary {
  connected: boolean;
  deviceId: string | null;
  pressedCount: number;
  timestampMs: number;
}

export interface NativeGamepadSnapshot {
  connected: boolean;
  deviceId: string | null;
  standardMask: number;
  timestampMs: number;
}
