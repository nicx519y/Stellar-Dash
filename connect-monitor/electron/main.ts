import { app, BrowserWindow, Menu, ipcMain, dialog } from "electron";
import fs from "node:fs/promises";
import path from "node:path";

import { MonitorEventBus } from "./pipeline/event-bus";
import { MonitorEventStore } from "./pipeline/event-store";
import { parseDongleTelemetryLine } from "./sources/dongle-telemetry-source";
import { getHidDebugConfigStatus, sendDebugConfig, startHidTelemetrySource } from "./sources/hid-telemetry-source";
import { SerialLogManager } from "./sources/serial-log-manager";
import { startSerialTelemetrySource } from "./sources/serial-telemetry-source";
import type { DebugConfig, DebugConfigStatus, SerialLogLine } from "../shared/monitor-types";

const eventStore = new MonitorEventStore(path.join(app.getPath("userData"), "db"));
const eventBus = new MonitorEventBus(500, eventStore);
let stopHidSource: (() => void) | null = null;
let stopSerialSource: (() => void) | null = null;
let mainWindow: BrowserWindow | null = null;
const pendingEvents: unknown[] = [];
const pendingSerialLogs: SerialLogLine[] = [];
const serialLogManager = new SerialLogManager((lines) => {
  pendingSerialLogs.push(...lines);
});
let paused = false;
let isShuttingDown = false;
const debugConfigPath = path.join(app.getPath("userData"), "debug-config.json");
const allowedManualChannels = [2, 11, 14, 24, 27, 35, 39];
let debugConfig: DebugConfig = {
  hidTelemetryEnabled: false,
  hidPeriodMs: 500,
  autoHopEnabled: true,
  manualChannel: null,
};

type ExportMarkdownRequest = {
  suggestedFileName?: string;
  content?: string;
};

function stopSources(): void {
  if (stopHidSource) {
    stopHidSource();
    stopHidSource = null;
  }
  if (stopSerialSource) {
    stopSerialSource();
    stopSerialSource = null;
  }
}

function clearRuntimeDatabase(): void {
  while (pendingEvents.length) pendingEvents.pop();
  eventBus.clear();
}

function sanitizeDebugConfig(value: unknown): DebugConfig {
  const cfg = value as Partial<DebugConfig> | null | undefined;
  const period = cfg?.hidPeriodMs;
  const hidPeriodMs = period === 100 || period === 250 || period === 500 || period === 1000 ? period : 500;
  const manualChannel = typeof cfg?.manualChannel === "number" && allowedManualChannels.includes(cfg.manualChannel)
    ? cfg.manualChannel
    : null;
  return {
    hidTelemetryEnabled: Boolean(cfg?.hidTelemetryEnabled),
    hidPeriodMs,
    autoHopEnabled: cfg?.autoHopEnabled !== false,
    manualChannel,
  };
}

async function loadDebugConfig(): Promise<void> {
  try {
    const raw = await fs.readFile(debugConfigPath, "utf8");
    debugConfig = sanitizeDebugConfig(JSON.parse(raw));
  } catch (_err) {
    debugConfig = sanitizeDebugConfig(debugConfig);
  }
}

async function saveDebugConfig(config: DebugConfig): Promise<void> {
  await fs.mkdir(path.dirname(debugConfigPath), { recursive: true });
  await fs.writeFile(debugConfigPath, JSON.stringify(config, null, 2), "utf8");
}

function applyDebugConfigToDevice(): DebugConfigStatus {
  return sendDebugConfig(debugConfig);
}

function shutdownAndClearDatabase(): void {
  if (isShuttingDown) return;
  isShuttingDown = true;
  stopSources();
  serialLogManager.dispose();
  clearRuntimeDatabase();
}

function createWindow(): void {
  const win = new BrowserWindow({
    width: 1280,
    height: 790,
    backgroundColor: "#0b0f16",
    frame: false,
    autoHideMenuBar: true,
    webPreferences: {
      preload: path.join(__dirname, "preload.js"),
      contextIsolation: true,
      nodeIntegration: false,
    },
  });
  win.setMenuBarVisibility(false);
  win.on("maximize", () => {
    win.webContents.send("window:state", { maximized: true });
  });
  win.on("unmaximize", () => {
    win.webContents.send("window:state", { maximized: false });
  });

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

app.whenReady().then(async () => {
  Menu.setApplicationMenu(null);
  await loadDebugConfig();
  clearRuntimeDatabase();
  if (process.env.MONITOR_MOCK === "1") {
    bootstrapMockInput();
  }
  stopHidSource = startHidTelemetrySource(
    (event) => {
      if (!paused) {
        eventBus.publish(event);
      }
    },
    { onControlReady: applyDebugConfigToDevice },
  );
  if (process.env.MONITOR_SERIAL_ENABLE === "1" || process.env.MONITOR_SERIAL_PATH) {
    stopSerialSource = startSerialTelemetrySource((event) => {
      if (!paused) {
        eventBus.publish(event);
      }
    });
  }
  eventBus.subscribe((event) => {
    pendingEvents.push(event);
  });
  createWindow();
});

ipcMain.handle("monitor:getSnapshot", (_evt, limit?: number) => {
  return eventBus.snapshot(typeof limit === "number" ? limit : 500);
});

ipcMain.handle("monitor:queryEvents", (_evt, beforeTimestampMs: number, limit?: number) => {
  return eventStore.queryBefore(beforeTimestampMs, typeof limit === "number" ? limit : 500);
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
    if (stopSerialSource) {
      stopSerialSource();
      stopSerialSource = null;
    }
    return;
  }
  if (!stopHidSource) {
    stopHidSource = startHidTelemetrySource(
      (event) => {
        if (!paused) {
          eventBus.publish(event);
        }
      },
      { onControlReady: applyDebugConfigToDevice },
    );
  }
  if (!stopSerialSource && (process.env.MONITOR_SERIAL_ENABLE === "1" || process.env.MONITOR_SERIAL_PATH)) {
    stopSerialSource = startSerialTelemetrySource((event) => {
      if (!paused) {
        eventBus.publish(event);
      }
    });
  }
});

ipcMain.handle("monitor:exportMarkdown", async (event, request: ExportMarkdownRequest) => {
  const win = BrowserWindow.fromWebContents(event.sender) ?? mainWindow;
  const suggestedFileName = request?.suggestedFileName?.trim() || "connect-monitor-log.md";
  const content = typeof request?.content === "string" ? request.content : "";
  const dialogOptions = {
    title: "Export Log",
    defaultPath: suggestedFileName.endsWith(".md") ? suggestedFileName : `${suggestedFileName}.md`,
    filters: [{ name: "Markdown", extensions: ["md"] }],
  };
  const result = win
    ? await dialog.showSaveDialog(win, dialogOptions)
    : await dialog.showSaveDialog(dialogOptions);
  if (result.canceled || !result.filePath) {
    return { canceled: true };
  }
  await fs.writeFile(result.filePath, content, "utf8");
  return { canceled: false, filePath: result.filePath };
});

ipcMain.handle("monitor:getDebugConfig", () => {
  return debugConfig;
});

ipcMain.handle("monitor:setDebugConfig", async (_event, nextConfig: unknown) => {
  debugConfig = sanitizeDebugConfig(nextConfig);
  await saveDebugConfig(debugConfig);
  return applyDebugConfigToDevice();
});

ipcMain.handle("monitor:getDebugConfigStatus", () => {
  return getHidDebugConfigStatus();
});

ipcMain.handle("serial:listPorts", () => {
  return serialLogManager.listPorts();
});

ipcMain.handle("serial:getLogSelections", () => {
  return serialLogManager.getSelections();
});

ipcMain.handle("serial:setLogSelections", (_event, selections: Array<string | null | undefined>) => {
  return serialLogManager.setSelections(Array.isArray(selections) ? selections : []);
});

ipcMain.handle("window:minimize", (event) => {
  BrowserWindow.fromWebContents(event.sender)?.minimize();
});

ipcMain.handle("window:toggleMaximize", (event) => {
  const win = BrowserWindow.fromWebContents(event.sender);
  if (!win) return false;
  if (win.isMaximized()) {
    win.unmaximize();
  } else {
    win.maximize();
  }
  return win.isMaximized();
});

ipcMain.handle("window:close", (event) => {
  BrowserWindow.fromWebContents(event.sender)?.close();
});

ipcMain.handle("window:getState", (event) => {
  const win = BrowserWindow.fromWebContents(event.sender);
  return { maximized: Boolean(win?.isMaximized()) };
});

setInterval(() => {
  if (!mainWindow) return;
  if (pendingEvents.length === 0) return;
  const batch = pendingEvents.splice(0, pendingEvents.length);
  mainWindow.webContents.send("monitor:events", batch);
}, 100);

setInterval(() => {
  if (!mainWindow) return;
  if (pendingSerialLogs.length === 0) return;
  const batch = pendingSerialLogs.splice(0, pendingSerialLogs.length);
  mainWindow.webContents.send("serial:logs", batch);
}, 100);

app.on("window-all-closed", () => {
  shutdownAndClearDatabase();
  app.quit();
});

app.on("before-quit", () => {
  shutdownAndClearDatabase();
});
