import { contextBridge, ipcRenderer } from "electron";
import type { HitboxBounds, HitboxOptions, HitboxSummary, LatencyTableBounds } from "../shared/monitor-types";

contextBridge.exposeInMainWorld("connectMonitorApi", {
  getVersion: () => "0.1.0",
  onEvents: (handler: (events: unknown[]) => void) => {
    const listener = (_event: unknown, events: unknown[]) => {
      handler(events);
    };
    ipcRenderer.on("monitor:events", listener);
    return () => ipcRenderer.off("monitor:events", listener);
  },
  onMonitorCleared: (handler: () => void) => {
    const listener = () => {
      handler();
    };
    ipcRenderer.on("monitor:cleared", listener);
    return () => ipcRenderer.off("monitor:cleared", listener);
  },
  getSnapshot: (limit?: number) => ipcRenderer.invoke("monitor:getSnapshot", limit),
  queryEvents: (beforeTimestampMs: number, limit?: number) => ipcRenderer.invoke("monitor:queryEvents", beforeTimestampMs, limit),
  clear: () => ipcRenderer.invoke("monitor:clear"),
  setPaused: (paused: boolean) => ipcRenderer.invoke("monitor:setPaused", paused),
  getPaused: () => ipcRenderer.invoke("monitor:getPaused"),
  exportMarkdown: (request: { suggestedFileName: string; content: string }) => ipcRenderer.invoke("monitor:exportMarkdown", request),
  getDebugConfig: () => ipcRenderer.invoke("monitor:getDebugConfig"),
  setDebugConfig: (config: unknown) => ipcRenderer.invoke("monitor:setDebugConfig", config),
  getDebugConfigStatus: () => ipcRenderer.invoke("monitor:getDebugConfigStatus"),
  listSerialPorts: () => ipcRenderer.invoke("serial:listPorts"),
  getSerialLogSelections: () => ipcRenderer.invoke("serial:getLogSelections"),
  setSerialLogSelections: (selections: Array<string | null | undefined>) => ipcRenderer.invoke("serial:setLogSelections", selections),
  onSerialLogs: (handler: (lines: unknown[]) => void) => {
    const listener = (_event: unknown, lines: unknown[]) => {
      handler(lines);
    };
    ipcRenderer.on("serial:logs", listener);
    return () => ipcRenderer.off("serial:logs", listener);
  },
  minimizeWindow: () => ipcRenderer.invoke("window:minimize"),
  toggleMaximizeWindow: () => ipcRenderer.invoke("window:toggleMaximize"),
  closeWindow: () => ipcRenderer.invoke("window:close"),
  getWindowState: () => ipcRenderer.invoke("window:getState"),
  onWindowState: (handler: (state: { maximized: boolean }) => void) => {
    const listener = (_event: unknown, state: { maximized: boolean }) => {
      handler(state);
    };
    ipcRenderer.on("window:state", listener);
    return () => ipcRenderer.off("window:state", listener);
  },
  setHitboxBounds: (bounds: HitboxBounds) => {
    ipcRenderer.send("hitbox:setBounds", bounds);
  },
  setLatencyTableBounds: (bounds: LatencyTableBounds) => {
    ipcRenderer.send("latencyTable:setBounds", bounds);
  },
  onHitboxSummary: (handler: (summary: HitboxSummary) => void) => {
    const listener = (_event: unknown, summary: HitboxSummary) => {
      handler(summary);
    };
    ipcRenderer.on("hitbox:summary", listener);
    return () => ipcRenderer.off("hitbox:summary", listener);
  },
  publishHitboxSummary: (summary: HitboxSummary) => {
    ipcRenderer.send("hitbox:summary", summary);
  },
  onHitboxOptions: (handler: (options: HitboxOptions) => void) => {
    const listener = (_event: unknown, options: HitboxOptions) => {
      handler(options);
    };
    ipcRenderer.on("hitbox:options", listener);
    return () => ipcRenderer.off("hitbox:options", listener);
  },
});
