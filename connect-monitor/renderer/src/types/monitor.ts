export type ConnectionMode = "USB" | "RF24G";
export type LinkState =
  | "Disconnected"
  | "Pairing"
  | "Connecting"
  | "Connected"
  | "Reconnecting"
  | "Error";

export interface MonitorOverview {
  mode: ConnectionMode;
  state: LinkState;
  targetRateHz: number;
  actualRateHz: number;
  latestUsbLatencyUs?: number;
  latestRfLatencyUs?: number;
  errorCount: number;
}
