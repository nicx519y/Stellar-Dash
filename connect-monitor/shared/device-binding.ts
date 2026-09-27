import type { ConnectionMode } from "./monitor-types";

export type BindingState = "connected" | "disconnected" | "ambiguous" | "unsupported";
export interface GamepadDevice {
  id: string;
  name: string;
  vendorId: number;
  productId: number;
  backend: "wgi" | "xinput";
  sourceMode?: ConnectionMode | null;
  generation: number;
}
export interface TelemetryDevice {
  id: string;
  path: string;
  vendorId: number;
  productId: number;
  serialNumber?: string;
  interfaceNumber?: number;
  sourceMode: ConnectionMode | null;
  capable: boolean;
  generation: number;
}
export interface BindingChoice {
  gamepadId: string | null;
  telemetryId: string | null;
}
export interface SourceBinding extends BindingChoice {
  sourceMode: ConnectionMode;
  state: BindingState;
  reason: string;
  generation: number;
  telemetryAvailable: boolean;
}
export interface DeviceBindings {
  activeSource: ConnectionMode;
  gamepads: GamepadDevice[];
  telemetry: TelemetryDevice[];
  bindings: Record<ConnectionMode, SourceBinding>;
}
