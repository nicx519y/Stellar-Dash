import { app, BrowserWindow, Menu, ipcMain } from "electron";
import path from "node:path";

import { MonitorEventBus } from "./pipeline/event-bus";
import { parseDongleTelemetryLine } from "./sources/dongle-telemetry-source";
import { startHidTelemetrySource } from "./sources/hid-telemetry-source";

const eventBus = new MonitorEventBus();
let stopHidSource: (() => void) | null = null;
let mainWindow: BrowserWindow | null = null;
const pendingEvents: unknown[] = [];
let paused = false;

function createWindow(): void {
  const win = new BrowserWindow({
    width: 1280,
    height: 800,
    backgroundColor: "#0b0f16",
    autoHideMenuBar: true,
    webPreferences: {
      preload: path.join(__dirname, "preload.js"),
      contextIsolation: true,
      nodeIntegration: false,
    },
  });
  win.setMenuBarVisibility(false);

  mainWindow = win;
  const devUrl = process.env.VITE_DEV_SERVER_URL;
  if (devUrl) {
    win.loadURL(devUrl);
  } else {
    win.loadFile(path.join(__dirname, "..", "renderer", "index.html"));
  }
}

function bootstrapMockInput(): void {
  const samples = [
    "MON|TYPE=STATUS|MODE=USB|STATE=Connected|TARGET=1000|ACTUAL=998",
    "MON|TYPE=LATENCY|SEQ=1|D2U=830",
    "MON|TYPE=STATUS|MODE=RF24G|STATE=Connected|TARGET=2000|ACTUAL=1988",
    "MON|TYPE=LATENCY|SEQ=2|D2U=910|D2R=420|R2U=240",
  ];
  for (const line of samples) {
    for (const event of parseDongleTelemetryLine(line)) {
      eventBus.publish(event);
    }
  }
}

app.whenReady().then(() => {
  Menu.setApplicationMenu(null);
  if (process.env.MONITOR_MOCK === "1") {
    bootstrapMockInput();
  }
  stopHidSource = startHidTelemetrySource((event) => {
    if (!paused) {
      eventBus.publish(event);
    }
  });
  eventBus.subscribe((event) => {
    pendingEvents.push(event);
  });
  createWindow();
});

ipcMain.handle("monitor:getSnapshot", (_evt, limit?: number) => {
  return eventBus.snapshot(typeof limit === "number" ? limit : 200);
});

ipcMain.handle("monitor:clear", () => {
  while (pendingEvents.length) pendingEvents.pop();
  eventBus.clear();
});

ipcMain.handle("monitor:getPaused", () => paused);

ipcMain.handle("monitor:setPaused", (_evt, nextPaused: boolean) => {
  paused = Boolean(nextPaused);
  while (pendingEvents.length) pendingEvents.pop();
  if (paused) {
    if (stopHidSource) {
      stopHidSource();
      stopHidSource = null;
    }
    return;
  }
  if (!stopHidSource) {
    stopHidSource = startHidTelemetrySource((event) => {
      if (!paused) {
        eventBus.publish(event);
      }
    });
  }
});

setInterval(() => {
  if (!mainWindow) return;
  if (pendingEvents.length === 0) return;
  const batch = pendingEvents.splice(0, pendingEvents.length);
  mainWindow.webContents.send("monitor:events", batch);
}, 100);

app.on("window-all-closed", () => {
  if (stopHidSource) {
    stopHidSource();
    stopHidSource = null;
  }
  app.quit();
});
