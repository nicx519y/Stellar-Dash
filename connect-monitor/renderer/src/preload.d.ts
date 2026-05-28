import type { MonitorEvent } from "../../shared/monitor-types";

declare global {
  interface Window {
    connectMonitorApi: {
      getVersion(): string;
      onEvents(handler: (events: MonitorEvent[]) => void): () => void;
      getSnapshot(limit?: number): Promise<MonitorEvent[]>;
      queryEvents(beforeTimestampMs: number, limit?: number): Promise<MonitorEvent[]>;
      clear(): Promise<void>;
      setPaused(paused: boolean): Promise<void>;
      getPaused(): Promise<boolean>;
      exportMarkdown(request: { suggestedFileName: string; content: string }): Promise<{ canceled: boolean; filePath?: string }>;
      minimizeWindow(): Promise<void>;
      toggleMaximizeWindow(): Promise<boolean>;
      closeWindow(): Promise<void>;
      getWindowState(): Promise<{ maximized: boolean }>;
      onWindowState(handler: (state: { maximized: boolean }) => void): () => void;
    };
  }
}

export {};
