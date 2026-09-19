import { app } from "electron";
import fs from "node:fs";
import path from "node:path";

export function startRuntimeDiagnostics(queueStats: () => unknown): () => void {
  const file = path.join(app.getPath("userData"), "runtime-diagnostics.jsonl");
  const record = (kind: string, data: unknown) => {
    try {
      if (fs.existsSync(file) && fs.statSync(file).size >= 2 * 1024 * 1024) {
        fs.rmSync(`${file}.1`, { force: true });
        fs.renameSync(file, `${file}.1`);
      }
      fs.appendFileSync(file, JSON.stringify({ timestampMs: Date.now(), pid: process.pid, kind, data }) + "\n");
    } catch { /* Diagnostics must remain usable even when the disk is full. */ }
  };
  const sample = () => record("memory", {
    main: process.memoryUsage(),
    processes: app.getAppMetrics().map(({ pid, type, memory }) => ({ pid, type, memory })),
    queues: queueStats(),
  });
  // Observe fatal JS errors without swallowing them and resuming a broken process.
  process.on("uncaughtExceptionMonitor", (error, origin) => record("uncaught-exception", {
    origin, message: error.message, stack: error.stack,
  }));
  app.on("render-process-gone", (_event, contents, details) => {
    record("render-process-gone", { webContentsId: contents.id, ...details });
  });
  app.on("child-process-gone", (_event, details) => record("child-process-gone", details));
  record("start", { version: app.getVersion(), electron: process.versions.electron });
  sample();
  const timer = setInterval(sample, 30_000);
  timer.unref();
  return () => { clearInterval(timer); record("shutdown", queueStats()); };
}
