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
  clear: () => ipcRenderer.invoke("monitor:clear"),
  setPaused: (paused: boolean) => ipcRenderer.invoke("monitor:setPaused", paused),
  getPaused: () => ipcRenderer.invoke("monitor:getPaused"),
});
