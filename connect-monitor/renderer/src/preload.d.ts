import type { FastRequest } from "../../shared/fast-recovery";
import type { BindingChoice, DeviceBindings } from "../../shared/device-binding";
import type { ConnectionMode } from "../../shared/monitor-types";
import type {
  DebugConfig,
  DebugConfigStatus,
  HitboxBounds,
  HitboxOptions,
  HitboxSummary,
  NativeGamepadSnapshot,
  LatencyTableBounds,
  MonitorEvent,
  SerialLogLine,
  SerialPortInfo,
} from "../../shared/monitor-types";

declare global {
  interface Window {
    connectMonitorApi: {
      getVersion(): string;
      getNativeGamepad(): Promise<NativeGamepadSnapshot | null>;
      getHitboxOptions(): Promise<HitboxOptions|null>;
      getDeviceBindings(): Promise<DeviceBindings>;
      setDeviceSource(mode:ConnectionMode):Promise<void>;
      selectDevices(mode:ConnectionMode,choice:BindingChoice|null):Promise<DeviceBindings>;
      onDeviceBindings(handler:(value:DeviceBindings)=>void):()=>void;
      onEvents(handler: (events: MonitorEvent[]) => void | Promise<void>): () => void;
      onMonitorCleared(handler: () => void): () => void;
      getSnapshot(limit?: number): Promise<MonitorEvent[]>;
      queryEvents(beforeTimestampMs: number, limit?: number): Promise<MonitorEvent[]>;
      clear(): Promise<void>;
      setPaused(paused: boolean): Promise<void>;
      getPaused(): Promise<boolean>;
      exportMarkdown(request: { suggestedFileName: string; content: string }): Promise<{ canceled: boolean; filePath?: string }>;
      fastRecovery(request: FastRequest): Promise<{ok:boolean;message?:string}>;
      exportFastLog(request: {content:string}): Promise<{canceled:boolean;filePath?:string}>;
      getDebugConfig(): Promise<DebugConfig>;
      setDebugConfig(config: DebugConfig): Promise<DebugConfigStatus>;
      getDebugConfigStatus(): Promise<DebugConfigStatus>;
      listSerialPorts(): Promise<SerialPortInfo[]>;
      getSerialLogSelections(): Promise<string[]>;
      setSerialLogSelections(selections: Array<string | null | undefined>): Promise<string[]>;
      onSerialLogs(handler: (lines: SerialLogLine[]) => void | Promise<void>): () => void;
      minimizeWindow(): Promise<void>;
      toggleMaximizeWindow(): Promise<boolean>;
      closeWindow(): Promise<void>;
      getWindowState(): Promise<{ maximized: boolean }>;
      onWindowState(handler: (state: { maximized: boolean }) => void): () => void;
      setHitboxBounds(bounds: HitboxBounds): void;
      setLatencyTableBounds(bounds: LatencyTableBounds): void;
      onHitboxSummary(handler: (summary: HitboxSummary) => void): () => void;
      publishHitboxSummary(summary: HitboxSummary): void;
      onHitboxOptions(handler: (options: HitboxOptions) => void): () => void;
    };
  }
}

export {};
