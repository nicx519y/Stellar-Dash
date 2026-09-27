import { app, BrowserWindow, Menu, ipcMain, dialog, WebContentsView, type Rectangle } from "electron";
import fs from "node:fs/promises";
import path from "node:path";

import { MonitorEventBus } from "./pipeline/event-bus";
import { AsyncMonitorEventStore } from "./pipeline/async-event-store";
import { BoundedDelivery } from "./pipeline/bounded-delivery";
import { startRuntimeDiagnostics } from "./runtime-diagnostics";
import { parseDongleTelemetryLine } from "./sources/dongle-telemetry-source";
import { getHidDebugConfigStatus, getHidQueueStats, sendDebugConfig, sendFastRecovery, startHidTelemetrySource, waitForHidShutdown, selectTelemetryDevices } from "./sources/hid-telemetry-client";
import { SerialLogManager } from "./sources/serial-log-manager";
import { startSerialTelemetrySource } from "./sources/serial-telemetry-source";
import { GamepadService } from "./sources/gamepad-service";
import type { BindingChoice, TelemetryDevice } from "../shared/device-binding";
import type { ConnectionMode, MonitorEvent } from "../shared/monitor-types";
import type {
  DebugConfig,
  DebugConfigStatus,
  HitboxBounds,
  HitboxOptions,
  HitboxSummary,
  LatencyTableBounds,
  SerialLogLine,
} from "../shared/monitor-types";

// Acquire the profile lock before creating stores, sessions or HID readers.
// A duplicate must exit immediately, before startup/shutdown can clear the live DB.
if (!app.requestSingleInstanceLock()) {
  app.exit(0);
}

const eventStore = new AsyncMonitorEventStore(path.join(app.getPath("userData"), "db"));
const eventBus = new MonitorEventBus(500, eventStore);
let stopHidSource: (() => void) | null = null;
let stopSerialSource: (() => void) | null = null;
let mainWindow: BrowserWindow | null = null;
app.on("second-instance", () => {
  if (!mainWindow || mainWindow.isDestroyed()) return;
  if (mainWindow.isMinimized()) mainWindow.restore();
  mainWindow.show();
  mainWindow.focus();
});
let hitboxView: WebContentsView | null = null;
let latencyTableView: WebContentsView | null = null;
const mainEvents = new BoundedDelivery<unknown>();
const latencyEvents = new BoundedDelivery<unknown>();
const serialLogs = new BoundedDelivery<SerialLogLine>();
let mainEventsReady = false;
let latencyEventsReady = false;
let serialLogsReady = false;
let stopDiagnostics: (() => void) | undefined;
const serialLogManager = new SerialLogManager((lines) => {
  serialLogs.enqueue(lines);
});
const appIconPath = path.resolve(__dirname, "..", "..", "resources", "icon.ico");
let paused = false;
let isShuttingDown = false;
let shutdownComplete = false;
const debugConfigPath = path.join(app.getPath("userData"), "debug-config.json");
let debugConfig: DebugConfig = {
  hidTelemetryEnabled: true,
  hidPeriodMs: 250,
  autoHopEnabled: true,
  manualChannel: null,
};

const bindingPath = path.join(app.getPath("userData"), "device-bindings.json");
const gamepads = new GamepadService(refreshBindings);
let bindingSignature = "";
let controlBindingSignature = "";
let telemetryViewSignature = "";
let compactHitbox = true;
let saveBindingTail: Promise<void> = Promise.resolve();
function hitboxOptions(): HitboxOptions {
  const binding=gamepads.registry.binding(gamepads.registry.activeSource);
  return {compact:compactHitbox,sourceMode:binding.sourceMode,bindingGeneration:binding.generation};
}
function refreshBindings() {
  if(isShuttingDown)return;
  const state=gamepads.registry.snapshot();
  const targets={USB:state.bindings.USB.telemetryAvailable?state.bindings.USB.telemetryId:null,
    RF24G:state.bindings.RF24G.telemetryAvailable?state.bindings.RF24G.telemetryId:null};
  const controls=JSON.stringify(targets);
  if(controls!==controlBindingSignature) {controlBindingSignature=controls;selectTelemetryDevices(targets);}
  const viewSignature=JSON.stringify([targets,state.telemetry.filter(d=>d.id===targets.USB||d.id===targets.RF24G).map(d=>[d.id,d.generation])]);
  if(telemetryViewSignature && telemetryViewSignature!==viewSignature) {
    // Keep persisted logs, but never display aggregates from the previous physical peer.
    mainEvents.clear();latencyEvents.clear();broadcastMonitorCleared();
  }
  telemetryViewSignature=viewSignature;
  gamepads.select();
  const signature=JSON.stringify(state);
  if(signature===bindingSignature)return;
  bindingSignature=signature;
  mainWindow?.webContents.send("devices:changed",state);
  latencyTableView?.webContents.send("devices:changed",state);
  hitboxView?.webContents.send("hitbox:options",hitboxOptions());
}
function telemetryDevicesChanged(devices:TelemetryDevice[]) {
  gamepads.registry.telemetry=devices;
  refreshBindings();
}
function isCurrentDeviceEvent(event:MonitorEvent) {
  if(!event.deviceId)return true;
  const mode=event.sourceMode??(event.kind==="device_status"?event.mode:undefined);
  if(!mode)return true;
  const binding=gamepads.registry.binding(mode);
  const identity=gamepads.registry.telemetry.find(d=>d.id===event.deviceId);
  return binding.telemetryId===event.deviceId && (event.deviceGeneration===undefined || identity?.generation===event.deviceGeneration);
}
function persistBindings() {
  // XInput slots are deliberately session-only; they must never survive restart.
  const choices=Object.fromEntries(Object.entries(gamepads.registry.choices).filter(([,c])=>!c.gamepadId?.startsWith("xinput:")));
  const json=JSON.stringify(choices,null,2);
  saveBindingTail=saveBindingTail.catch(()=>{}).then(async()=>{
    await fs.mkdir(path.dirname(bindingPath),{recursive:true});
    await fs.writeFile(bindingPath+".tmp",json,"utf8");
    await fs.rename(bindingPath+".tmp",bindingPath);
  });
  return saveBindingTail;
}

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
    options: { ...hitboxOptions(), compact: bounds?.compact !== false },
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
    sourceMode: summary?.sourceMode === "USB" ? "USB" : "RF24G",
    bindingGeneration: typeof summary?.bindingGeneration === "number" ? summary.bindingGeneration : -1,
    reason: typeof summary?.reason === "string" ? summary.reason.slice(0,256) : "",
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
  view.webContents.on("did-start-loading", () => {
    latencyEventsReady = false;
    latencyEvents.reset();
  });
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
  mainEvents.clear();
  latencyEvents.clear();
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
    latencyMeasurementEnabled: cfg?.latencyMeasurementEnabled === true,
    hidPeriodMs,
    sourceMode: cfg?.sourceMode === "USB" ? "USB" : "RF24G",
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

async function shutdownAndClearDatabase(): Promise<void> {
  if (isShuttingDown) return;
  isShuttingDown = true;
  gamepads.stop();
  stopSources();
  serialLogManager.dispose();
  clearRuntimeDatabase();
  serialLogs.clear();
  stopDiagnostics?.();
  await Promise.race([
    Promise.allSettled([eventStore.close(), waitForHidShutdown(),saveBindingTail]),
    new Promise(resolve => setTimeout(resolve, 2500)),
  ]);
}

function createWindow(): void {
  const win = new BrowserWindow({
    title: "Conn-Monitor",
    width: 1800,
    height: 1000,
    minWidth: 1800,
    minHeight: 1000,
    backgroundColor: "#0b0f16",
    frame: false,
    autoHideMenuBar: true,
    icon: appIconPath,
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
  win.webContents.on("did-start-loading", () => {
    mainEventsReady = false;
    serialLogsReady = false;
    mainEvents.reset();
    serialLogs.reset();
  });
  createHitboxView(win);
  createLatencyTableView(win);
  win.on("closed", () => {
    if (mainWindow === win) {
      mainWindow = null;
    }
    // WebContentsView does not own the lifetime of its WebContents.
    for (const view of [hitboxView, latencyTableView]) {
      if (view && !view.webContents.isDestroyed()) view.webContents.close();
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
  stopDiagnostics = startRuntimeDiagnostics(() => ({
    main: mainEvents.stats(), latency: latencyEvents.stats(), serial: serialLogs.stats(),
    historyWriteError: eventStore.lastWriteError,
    history: eventStore.stats(), hid: getHidQueueStats(),
  }));
  await loadDebugConfig();
  gamepads.registry.activeSource=debugConfig.sourceMode??"RF24G";
  try {
    const saved=JSON.parse(await fs.readFile(bindingPath,"utf8"));
    for(const mode of ["USB","RF24G"] as const) {
      const choice=saved[mode];
      if(choice && (choice.gamepadId===null || typeof choice.gamepadId==="string") &&
          (choice.telemetryId===null || typeof choice.telemetryId==="string") && !choice.gamepadId?.startsWith("xinput:"))
        gamepads.registry.choices[mode]={gamepadId:choice.gamepadId,telemetryId:choice.telemetryId};
    }
  } catch {}
  if(process.env.MONITOR_MOCK!=="1") {
    const helper=path.join(__dirname,"..","native","xora-gamepad-helper.exe").replace(/app\.asar([\\/])/i,"app.asar.unpacked$1");
    gamepads.start(helper);
  }
  clearRuntimeDatabase();
  if (process.env.MONITOR_MOCK === "1") {
    bootstrapMockInput();
  }
  stopHidSource = startHidTelemetrySource(
    (event) => {
      if (!paused && isCurrentDeviceEvent(event)) {
        eventBus.publish(event);
      }
    },
    { onControlReady: applyDebugConfigToDevice, onDevices:telemetryDevicesChanged },
  );
  if (process.env.MONITOR_SERIAL_ENABLE === "1" || process.env.MONITOR_SERIAL_PATH) {
    stopSerialSource = startSerialTelemetrySource((event) => {
      if (!paused) {
        eventBus.publish(event);
      }
    });
  }
  eventBus.subscribe((event) => {
    mainEvents.enqueue([event]);
    // The embedded table needs device lifecycle to select the same USB session, plus latency rows.
    if (event.kind === "device_status" || event.kind === "button_latency" || event.kind === "button_latency_status") {
      latencyEvents.enqueue([event]);
    }
  });
  createWindow();
});

ipcMain.handle("monitor:getSnapshot", (_evt, limit?: number) => {
  return eventBus.snapshot(typeof limit === "number" ? limit : 500).filter(isCurrentDeviceEvent);
});

ipcMain.handle("monitor:queryEvents", (_evt, beforeTimestampMs: number, limit?: number) => {
  return eventStore.queryBefore(beforeTimestampMs, typeof limit === "number" ? limit : 500);
});

ipcMain.handle("monitor:clear", () => {
  clearRuntimeDatabase();
  broadcastMonitorCleared();
});

ipcMain.handle("monitor:getPaused", () => paused);

ipcMain.handle("monitor:setPaused", (_evt, nextPaused: boolean) => {
  paused = Boolean(nextPaused);
  mainEvents.clear();
  latencyEvents.clear();
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
        if (!paused && isCurrentDeviceEvent(event)) {
          eventBus.publish(event);
        }
      },
      { onControlReady: applyDebugConfigToDevice, onDevices:telemetryDevicesChanged },
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

ipcMain.handle("monitor:fastRecovery", (_event, request) => sendFastRecovery(request));
ipcMain.handle("monitor:exportFastLog", async (event, request: {content:string}) => {
  if(typeof request?.content!=="string" || request.content.length>32*1024*1024)throw new Error("Invalid log size");
  const result=await dialog.showSaveDialog({title:"Export RF experiment",defaultPath:"rf-fast-experiment.jsonl",filters:[{name:"JSONL",extensions:["jsonl"]}]});
  if(result.canceled || !result.filePath)return {canceled:true};
  await fs.writeFile(result.filePath,request.content,"utf8");return {canceled:false,filePath:result.filePath};
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
  compactHitbox=nextBounds.options.compact;
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
  const value=sanitizeHitboxSummary(summary);
  const binding=gamepads.registry.binding(gamepads.registry.activeSource);
  if(value.sourceMode!==binding.sourceMode || value.bindingGeneration!==binding.generation)return;
  mainWindow.webContents.send("hitbox:summary", value);
});

ipcMain.handle("hitbox:getNativeGamepad", (event) => {
  if (!hitboxView || event.sender !== hitboxView.webContents) return null;
  return gamepads.registry.read(gamepads.readings);
});

ipcMain.handle("hitbox:getOptions",event=>{
  if(event.sender!==hitboxView?.webContents)return null;
  return hitboxOptions();
});
ipcMain.handle("devices:get",event=>{
  if(event.sender!==mainWindow?.webContents && event.sender!==latencyTableView?.webContents)return null;
  return gamepads.registry.snapshot();
});
ipcMain.handle("devices:source",(event,mode:ConnectionMode)=>{
  if(event.sender!==mainWindow?.webContents || (mode!=="USB" && mode!=="RF24G"))return;
  gamepads.registry.activeSource=mode;
  refreshBindings();
});
ipcMain.handle("devices:select",async(event,mode:ConnectionMode,choice:BindingChoice|null)=>{
  if(event.sender!==mainWindow?.webContents || (mode!=="USB" && mode!=="RF24G"))throw new Error("Invalid source");
  if(choice && ((choice.gamepadId!==null && typeof choice.gamepadId!=="string") ||
    (choice.telemetryId!==null && typeof choice.telemetryId!=="string")))throw new Error("Invalid selection");
  gamepads.registry.choose(mode,choice);
  refreshBindings();
  await persistBindings();
  return gamepads.registry.snapshot();
});

ipcMain.on("monitor:events:ready", (event) => {
  if (event.sender === mainWindow?.webContents) mainEventsReady = true;
  if (event.sender === latencyTableView?.webContents) latencyEventsReady = true;
});
ipcMain.on("serial:logs:ready", (event) => {
  if (event.sender === mainWindow?.webContents) serialLogsReady = true;
});
ipcMain.on("monitor:events:ack", (event, sequence: number) => {
  if (event.sender === mainWindow?.webContents) mainEvents.acknowledge(sequence);
  if (event.sender === latencyTableView?.webContents) latencyEvents.acknowledge(sequence);
  flushMonitorEvents();
});
ipcMain.on("serial:logs:ack", (event, sequence: number) => {
  if (event.sender === mainWindow?.webContents) serialLogs.acknowledge(sequence);
});

function flushMonitorEvents(): void {
  if (!mainWindow || mainWindow.webContents.isDestroyed()) return;
  if (mainEventsReady) mainEvents.flush((batch, sequence) => mainWindow!.webContents.send("monitor:events", batch, sequence));
  if (latencyEventsReady && latencyTableView && !latencyTableView.webContents.isDestroyed()) {
    latencyEvents.flush((batch, sequence) => latencyTableView!.webContents.send("monitor:events", batch, sequence));
  }
  if (serialLogsReady) serialLogs.flush((batch, sequence) => mainWindow!.webContents.send("serial:logs", batch, sequence));
}
setInterval(flushMonitorEvents, 100);

app.on("window-all-closed", () => {
  app.quit();
});

app.on("before-quit", event => {
  if (shutdownComplete) return;
  event.preventDefault();
  if (isShuttingDown) return;
  void shutdownAndClearDatabase().finally(() => { shutdownComplete = true; app.quit(); });
});
