import { app, BrowserWindow, Menu, ipcMain, dialog, WebContentsView, type Rectangle } from "electron";
import fs from "node:fs/promises";
import path from "node:path";

import { MonitorEventBus } from "./pipeline/event-bus";
import { MonitorEventStore } from "./pipeline/event-store";
import { parseDongleTelemetryLine } from "./sources/dongle-telemetry-source";
import { getHidDebugConfigStatus, sendDebugConfig, startHidTelemetrySource } from "./sources/hid-telemetry-source";
import { SerialLogManager } from "./sources/serial-log-manager";
import { startSerialTelemetrySource } from "./sources/serial-telemetry-source";
import type {
  DebugConfig,
  DebugConfigStatus,
  HitboxBounds,
  HitboxOptions,
  HitboxSummary,
  LatencyTableBounds,
  SerialLogLine,
} from "../shared/monitor-types";

const eventStore = new MonitorEventStore(path.join(app.getPath("userData"), "db"));
const eventBus = new MonitorEventBus(500, eventStore);
let stopHidSource: (() => void) | null = null;
let stopSerialSource: (() => void) | null = null;
let mainWindow: BrowserWindow | null = null;
let hitboxView: WebContentsView | null = null;
let latencyTableView: WebContentsView | null = null;
const pendingEvents: unknown[] = [];
const pendingSerialLogs: SerialLogLine[] = [];
const serialLogManager = new SerialLogManager((lines) => {
  pendingSerialLogs.push(...lines);
});
let paused = false;
let isShuttingDown = false;
const debugConfigPath = path.join(app.getPath("userData"), "debug-config.json");
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

type SanitizedHitboxBounds = {
  rect: Rectangle;
  visible: boolean;
  options: HitboxOptions;
};

type SanitizedLatencyTableBounds = {
  rect: Rectangle;
  visible: boolean;
};

function rendererUrl(pageName: string): string | null {
  const devUrl = process.env.VITE_DEV_SERVER_URL;
  if (!devUrl) return null;
  return new URL(pageName, devUrl.endsWith("/") ? devUrl : `${devUrl}/`).toString();
}

function loadHitboxRenderer(view: WebContentsView): void {
  const devHitboxUrl = rendererUrl("hitbox.html");
  if (devHitboxUrl) {
    view.webContents.loadURL(devHitboxUrl).catch(() => {});
    return;
  }
  view.webContents.loadFile(path.join(__dirname, "..", "renderer", "hitbox.html")).catch(() => {});
}

function loadLatencyTableRenderer(view: WebContentsView): void {
  const devLatencyTableUrl = rendererUrl("latency-table.html");
  if (devLatencyTableUrl) {
    view.webContents.loadURL(devLatencyTableUrl).catch(() => {});
    return;
  }
  view.webContents.loadFile(path.join(__dirname, "..", "renderer", "latency-table.html")).catch(() => {});
}

function sanitizeNumber(value: unknown): number | null {
  const next = Number(value);
  return Number.isFinite(next) ? next : null;
}

function sanitizeHitboxBounds(value: unknown): SanitizedHitboxBounds | null {
  const bounds = value as Partial<HitboxBounds> | null | undefined;
  const rawX = sanitizeNumber(bounds?.x);
  const rawY = sanitizeNumber(bounds?.y);
  const rawWidth = sanitizeNumber(bounds?.width);
  const rawHeight = sanitizeNumber(bounds?.height);
  if (rawX === null || rawY === null || rawWidth === null || rawHeight === null) {
    return null;
  }

  const contentBounds = mainWindow?.getContentBounds();
  const maxWidth = Math.max(1, contentBounds?.width ?? 4096);
  const maxHeight = Math.max(1, contentBounds?.height ?? 4096);
  const x = Math.max(0, Math.min(Math.round(rawX), maxWidth));
  const y = Math.max(0, Math.min(Math.round(rawY), maxHeight));
  const width = Math.max(0, Math.min(Math.round(rawWidth), maxWidth - x));
  const height = Math.max(0, Math.min(Math.round(rawHeight), maxHeight - y));
  const visible = bounds?.visible !== false && width >= 2 && height >= 2;

  return {
    rect: { x, y, width: Math.max(1, width), height: Math.max(1, height) },
    visible,
    options: { compact: bounds?.compact !== false },
  };
}

function sanitizeLatencyTableBounds(value: unknown): SanitizedLatencyTableBounds | null {
  const bounds = value as Partial<LatencyTableBounds> | null | undefined;
  const rawX = sanitizeNumber(bounds?.x);
  const rawY = sanitizeNumber(bounds?.y);
  const rawWidth = sanitizeNumber(bounds?.width);
  const rawHeight = sanitizeNumber(bounds?.height);
  if (rawX === null || rawY === null || rawWidth === null || rawHeight === null) {
    return null;
  }

  const contentBounds = mainWindow?.getContentBounds();
  const maxWidth = Math.max(1, contentBounds?.width ?? 4096);
  const maxHeight = Math.max(1, contentBounds?.height ?? 4096);
  const x = Math.max(0, Math.min(Math.round(rawX), maxWidth));
  const y = Math.max(0, Math.min(Math.round(rawY), maxHeight));
  const width = Math.max(0, Math.min(Math.round(rawWidth), maxWidth - x));
  const height = Math.max(0, Math.min(Math.round(rawHeight), maxHeight - y));
  const visible = bounds?.visible !== false && width >= 2 && height >= 2;

  return {
    rect: { x, y, width: Math.max(1, width), height: Math.max(1, height) },
    visible,
  };
}

function sanitizeHitboxSummary(value: unknown): HitboxSummary {
  const summary = value as Partial<HitboxSummary> | null | undefined;
  const pressedCount = sanitizeNumber(summary?.pressedCount);
  const timestampMs = sanitizeNumber(summary?.timestampMs);
  return {
    connected: Boolean(summary?.connected),
    deviceId: typeof summary?.deviceId === "string" && summary.deviceId.length > 0 ? summary.deviceId.slice(0, 256) : null,
    pressedCount: Math.max(0, Math.min(32, Math.round(pressedCount ?? 0))),
    timestampMs: timestampMs ?? Date.now(),
  };
}

function createHitboxView(win: BrowserWindow): void {
  const view = new WebContentsView({
    webPreferences: {
      preload: path.join(__dirname, "preload.js"),
      contextIsolation: true,
      nodeIntegration: false,
      backgroundThrottling: false,
    },
  });
  view.setBackgroundColor("#00000000");
  view.setVisible(false);
  win.contentView.addChildView(view);
  hitboxView = view;
  loadHitboxRenderer(view);
}

function createLatencyTableView(win: BrowserWindow): void {
  const view = new WebContentsView({
    webPreferences: {
      preload: path.join(__dirname, "preload.js"),
      contextIsolation: true,
      nodeIntegration: false,
      backgroundThrottling: false,
    },
  });
  view.setBackgroundColor("#00000000");
  view.setVisible(false);
  win.contentView.addChildView(view);
  latencyTableView = view;
  loadLatencyTableRenderer(view);
}

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

function broadcastMonitorCleared(): void {
  mainWindow?.webContents.send("monitor:cleared");
  latencyTableView?.webContents.send("monitor:cleared");
}

function sanitizeDebugConfig(value: unknown): DebugConfig {
  const cfg = value as Partial<DebugConfig> | null | undefined;
  const period = cfg?.hidPeriodMs;
  const hidPeriodMs = period === 100 || period === 250 || period === 500 || period === 1000 ? period : 500;
  const rawManualChannel = typeof cfg?.manualChannel === "number" ? cfg.manualChannel : NaN;
  const manualChannel = Number.isInteger(rawManualChannel) && rawManualChannel >= 0 && rawManualChannel <= 39
    ? rawManualChannel
    : null;
  const autoHopEnabled = cfg?.autoHopEnabled !== false || manualChannel === null;
  return {
    hidTelemetryEnabled: Boolean(cfg?.hidTelemetryEnabled),
    hidPeriodMs,
    autoHopEnabled,
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
  createHitboxView(win);
  createLatencyTableView(win);
  win.on("closed", () => {
    if (mainWindow === win) {
      mainWindow = null;
    }
    hitboxView = null;
    latencyTableView = null;
  });

  const devUrl = rendererUrl("index.html");
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
  broadcastMonitorCleared();
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

ipcMain.on("hitbox:setBounds", (event, bounds: unknown) => {
  if (!mainWindow || event.sender !== mainWindow.webContents || !hitboxView) return;
  const nextBounds = sanitizeHitboxBounds(bounds);
  if (!nextBounds) {
    hitboxView.setVisible(false);
    return;
  }

  if (!nextBounds.visible) {
    hitboxView.setVisible(false);
    return;
  }

  hitboxView.setBounds(nextBounds.rect);
  hitboxView.setVisible(true);
  hitboxView.webContents.send("hitbox:options", nextBounds.options);
});

ipcMain.on("latencyTable:setBounds", (event, bounds: unknown) => {
  if (!mainWindow || event.sender !== mainWindow.webContents || !latencyTableView) return;
  const nextBounds = sanitizeLatencyTableBounds(bounds);
  if (!nextBounds) {
    latencyTableView.setVisible(false);
    return;
  }

  if (!nextBounds.visible) {
    latencyTableView.setVisible(false);
    return;
  }

  latencyTableView.setBounds(nextBounds.rect);
  latencyTableView.setVisible(true);
});

ipcMain.on("hitbox:summary", (event, summary: unknown) => {
  if (!mainWindow || !hitboxView || event.sender !== hitboxView.webContents) return;
  mainWindow.webContents.send("hitbox:summary", sanitizeHitboxSummary(summary));
});

setInterval(() => {
  if (!mainWindow) return;
  if (pendingEvents.length === 0) return;
  const batch = pendingEvents.splice(0, pendingEvents.length);
  mainWindow.webContents.send("monitor:events", batch);
  latencyTableView?.webContents.send("monitor:events", batch);
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
