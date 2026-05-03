import type { MonitorEvent } from "../../shared/monitor-types";

declare global {
  interface Window {
    connectMonitorApi: {
      getVersion(): string;
      onEvents(handler: (events: MonitorEvent[]) => void): () => void;
      getSnapshot(limit?: number): Promise<MonitorEvent[]>;
      clear(): Promise<void>;
      setPaused(paused: boolean): Promise<void>;
      getPaused(): Promise<boolean>;
    };
  }
}

export {};
