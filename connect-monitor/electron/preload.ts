import { contextBridge, ipcRenderer } from "electron";

contextBridge.exposeInMainWorld("connectMonitorApi", {
  getVersion: () => "0.1.0",
  onEvents: (handler: (events: unknown[]) => void) => {
    const listener = (_event: unknown, events: unknown[]) => {
      handler(events);
    };
    ipcRenderer.on("monitor:events", listener);
    return () => ipcRenderer.off("monitor:events", listener);
  },
  getSnapshot: (limit?: number) => ipcRenderer.invoke("monitor:getSnapshot", limit),
  queryEvents: (beforeTimestampMs: number, limit?: number) => ipcRenderer.invoke("monitor:queryEvents", beforeTimestampMs, limit),
  clear: () => ipcRenderer.invoke("monitor:clear"),
  setPaused: (paused: boolean) => ipcRenderer.invoke("monitor:setPaused", paused),
  getPaused: () => ipcRenderer.invoke("monitor:getPaused"),
  exportMarkdown: (request: { suggestedFileName: string; content: string }) => ipcRenderer.invoke("monitor:exportMarkdown", request),
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
});
