import { app } from "electron";
import fs from "node:fs";
import path from "node:path";

export function startRuntimeDiagnostics(queueStats: () => unknown): () => void {
  const file = path.join(app.getPath("userData"), "runtime-diagnostics.jsonl");
  const pending: string[] = [];
  let writing = false;
  const drain = async () => {
    if (writing) return;
    writing = true;
    try {
      while (pending.length) {
        const lines = pending.splice(0).join("");
        try {
          const info = await fs.promises.stat(file).catch(() => null);
          if (info && info.size >= 2 * 1024 * 1024) {
            await fs.promises.rm(`${file}.1`, { force: true });
            await fs.promises.rename(file, `${file}.1`);
          }
          await fs.promises.appendFile(file, lines);
        } catch { /* Diagnostics must not interrupt live monitoring. */ }
      }
    } finally { writing = false; }
  };
  const record = (kind: string, data: unknown) => {
    pending.push(JSON.stringify({ timestampMs: Date.now(), pid: process.pid, kind, data }) + "\n");
    if (pending.length > 16) pending.shift();
    void drain();
  };
  const sample = () => record("memory", {
    main: process.memoryUsage(),
    processes: app.getAppMetrics().map(({ pid, type, memory }) => ({ pid, type, memory })),
    queues: queueStats(),
  });
  // Observe fatal JS errors without swallowing them and resuming a broken process.
  process.on("uncaughtExceptionMonitor", (error, origin) => {
    // Fatal-only synchronous write: the process may exit before async I/O runs.
    try { fs.appendFileSync(file, JSON.stringify({ timestampMs: Date.now(), pid: process.pid,
      kind: "uncaught-exception", data: { origin, message: error.message, stack: error.stack } }) + "\n"); } catch {}
  });
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
