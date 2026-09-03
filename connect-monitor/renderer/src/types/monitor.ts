export type ConnectionMode = "USB" | "RF24G";
export type LinkState = "Disconnected" | "Connecting" | "Connected" | "Error";

export interface MonitorOverview {
  mode: ConnectionMode;
  state: LinkState;
  targetRateHz: number;
  actualRateHz: number;
  latestUsbLatencyUs?: number;
  latestRfLatencyUs?: number;
  errorCount: number;
}
