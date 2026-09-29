import path from "node:path";
import { Worker } from "node:worker_threads";
import type { MonitorEvent } from "./types";

// Only one disk job may be outstanding. Slow disks cannot grow the MessagePort
// queue indefinitely or block live telemetry; discarded archive rows are counted.
export class AsyncMonitorEventStore {
  private readonly worker: Worker;
  private pending: MonitorEvent[] = [];
  private queries: { id: number; before: number; limit: number }[] = [];
  private replies = new Map<number, (rows: MonitorEvent[]) => void>();
  private timer: NodeJS.Timeout | null = null;
  private busy = false;
  private closed = false;
  private generation = 0;
  private sentGeneration = -1;
  private nextQuery = 0;
  private dropped = 0;
  private writeError: string | null = null;
  private closing: Promise<void> | null = null;

  constructor(baseDir: string) {
    this.worker = new Worker(path.join(__dirname, "event-store-worker.js"), { workerData: { baseDir } });
    this.worker.on("message", message => {
      this.busy = false;
      if (message.generation === this.generation) {
        this.writeError = message.error;
        for (const result of message.results) {
          this.replies.get(result.id)?.(result.rows);
          this.replies.delete(result.id);
        }
      }
      this.schedule();
    });
    this.worker.on("error", error => this.fail(error.message));
    this.worker.on("exit", code => { if (!this.closed) this.fail(`History worker exited (${code})`); });
    this.worker.unref();
  }

  get lastWriteError(): string | null { return this.writeError; }
  stats() { return { pending: this.pending.length, inFlight: this.busy, dropped: this.dropped, queries: this.replies.size }; }

  append(events: MonitorEvent[]): void {
    if (this.closed) { this.dropped += events.length; return; }
    this.pending.push(...events);
    if (this.pending.length > 5000) {
      const overflow = this.pending.length - 5000;
      this.pending.splice(0, overflow);
      this.dropped += overflow;
    }
    this.schedule();
  }

  clear(): void {
    this.generation++;
    this.pending = [];
    this.queries = [];
    for (const reply of this.replies.values()) reply([]);
    this.replies.clear();
    this.schedule();
  }

  queryBefore(before: number, limit: number): Promise<MonitorEvent[]> {
    if (this.closed || !Number.isFinite(before) || !Number.isFinite(limit) || limit <= 0) return Promise.resolve([]);
    if (this.replies.size >= 8) return Promise.reject(new Error("History reader busy"));
    return new Promise(resolve => {
      const id = ++this.nextQuery;
      this.replies.set(id, resolve);
      this.queries.push({ id, before, limit: Math.min(5000, Math.floor(limit)) });
      this.schedule();
    });
  }

  close(): Promise<void> {
    if (this.closing) return this.closing;
    if (this.closed) return Promise.resolve();
    this.clear();
    this.closed = true;
    if (this.timer) clearTimeout(this.timer);
    // FIFO ensures earlier writes finish before deletion, even during quit.
    this.closing = new Promise(resolve => {
      const timeout = setTimeout(() => { void this.worker.terminate(); resolve(); }, 2000);
      this.worker.once("exit", () => { clearTimeout(timeout); resolve(); });
      this.worker.postMessage({ type: "close" });
    });
    return this.closing;
  }

  private fail(message: string): void {
    this.writeError = message;
    this.closed = true;
    this.dropped += this.pending.length;
    this.pending = [];
    this.queries = [];
    if (this.timer) clearTimeout(this.timer);
    for (const reply of this.replies.values()) reply([]);
    this.replies.clear();
  }

  private schedule(): void {
    if (this.closed || this.busy || this.timer) return;
    if (!this.pending.length && !this.queries.length && this.sentGeneration === this.generation) return;
    this.timer = setTimeout(() => {
      this.timer = null;
      this.busy = true;
      this.sentGeneration = this.generation;
      this.worker.postMessage({ type: "work", generation: this.generation, events: this.pending, queries: this.queries });
      this.pending = [];
      this.queries = [];
    }, 50);
    this.timer.unref();
  }
}
