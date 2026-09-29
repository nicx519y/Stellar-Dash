// Run after npm run build: electron tests/electron-stream-smoke.cjs
// Uses an isolated profile and synthetic data; never opens HID/serial devices.
const { app, BrowserWindow, WebContentsView, ipcMain } = require('electron');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { BoundedDelivery } = require('../dist/electron/pipeline/bounded-delivery');
const { startRuntimeDiagnostics } = require('../dist/electron/runtime-diagnostics');
const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'monitor-stream-smoke-'));
app.setPath('userData', profile);
// Exercise IPC/Worker code without depending on a visible desktop GPU surface.
app.disableHardwareAcceleration();
const watchdog = setTimeout(() => { console.error('Electron smoke timeout'); app.exit(1); }, 45000);
const queue = new BoundedDelivery();
let ready = false, acknowledgements = 0, produced = 0, win, view;
const crashes = [], samples = [];
app.on('render-process-gone', (_e, _wc, details) => crashes.push(details));
ipcMain.handle('monitor:getSnapshot', () => []);
ipcMain.handle('monitor:getPaused', () => false);
ipcMain.on('monitor:events:ready', () => { ready = true; });
ipcMain.on('monitor:events:ack', (_event, sequence) => {
  acknowledgements++;
  queue.acknowledge(sequence);
});
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
const waitFor = async predicate => {
  const end = Date.now() + 10000;
  while (!predicate()) { assert.ok(Date.now() < end, 'renderer timeout'); await sleep(20); }
};
app.whenReady().then(async () => {
  let timer, stopDiagnostics;
  try {
    stopDiagnostics = startRuntimeDiagnostics(() => queue.stats());
    win = new BrowserWindow({ show: false, width: 1000, height: 600 });
    await win.loadURL('about:blank');
    view = new WebContentsView({ webPreferences: {
      preload: path.resolve(__dirname, '../dist/electron/preload.js'),
      contextIsolation: true, nodeIntegration: false, backgroundThrottling: false,
    } });
    win.contentView.addChildView(view);
    view.setBounds({ x: 0, y: 0, width: 1000, height: 600 });
    view.webContents.on('did-start-loading', () => { ready = false; queue.reset(); });
    await view.webContents.loadFile(path.resolve(__dirname, '../dist/renderer/latency-table.html'));
    console.log('latency renderer loaded');
    await waitFor(() => ready);
    timer = setInterval(() => {
      const timestampMs = Date.now();
      queue.enqueue(Array.from({ length: 100 }, () => ({
        kind: 'button_latency', timestampMs, inputSeq: produced++, keyMask: 1, standardMask: 1,
        previousStandardMask: 0, action: 'press', measurement: 'windows', latencyMs: 1,
        latencyMinMs: 0.5, latencyMaxMs: 1.5, confidence: 'high',
        sampleTickUs: 1, samplePcUs: 1, xinputPcUs: 1001,
      })));
      if (ready) queue.flush((batch, seq) => view.webContents.send('monitor:events', batch, seq));
    }, 10);
    for (let i = 0; i < 4; i++) {
      await sleep(5000);
      samples.push(app.getAppMetrics().map(({ type, memory }) => ({ type, memory })));
      assert.ok(acknowledgements > i * 10, 'async bridge/worker acknowledgement stopped');
      assert.ok(queue.stats().pending <= 2000);
      if (i === 1) {
        // Force a real Chromium receiver stall, then verify bounded recovery.
        await view.webContents.executeJavaScript('const end=performance.now()+750; while(performance.now()<end) {}');
        assert.ok(queue.stats().dropped > 0, 'test must exercise overload');
        view.webContents.reload();
        await waitFor(() => ready);
      }
    }
    assert.deepEqual(crashes, []);
    clearInterval(timer);
    clearTimeout(watchdog);
    stopDiagnostics();
    const diagnostics = fs.readFileSync(path.join(profile, 'runtime-diagnostics.jsonl'), 'utf8');
    assert.ok(diagnostics.includes('"kind":"memory"'));
    assert.ok(diagnostics.includes('"kind":"shutdown"'));
    view.webContents.close();
    win.destroy();
    console.log(JSON.stringify({ passed: true, produced, acknowledgements, queues: queue.stats(), samples }));
    app.exit(0);
  } catch (error) {
    clearInterval(timer);
    stopDiagnostics?.();
    console.error(error);
    app.exit(1);
  }
});
