import fs from "node:fs";
import path from "node:path";

import type { MonitorEvent } from "./types";

export class MonitorEventStore {
  private readonly filePath: string;
  private writeError: string | null = null;

  get lastWriteError(): string | null { return this.writeError; }

  constructor(baseDir: string) {
    this.filePath = path.join(baseDir, "monitor-events.jsonl");
    fs.mkdirSync(path.dirname(this.filePath), { recursive: true });
  }

  append(events: MonitorEvent[]): void {
    if (events.length === 0) return;
    const data = events.map((event) => JSON.stringify(event)).join("\n") + "\n";
    try {
      fs.appendFileSync(this.filePath, data);
      this.writeError = null;
    } catch (error) {
      const message = error instanceof Error ? error.message : String(error);
      if (this.writeError !== message) console.error("Monitor history write failed:", message);
      this.writeError = message;
      // Disk-full/permission failures must not escape a HID or worker callback
      // and terminate the monitor. Live events still reach the bounded UI cache.
    }
  }

  clear(): void {
    try {
      fs.rmSync(this.filePath, { force: true });
    } catch {
    }
  }

  queryBefore(beforeTimestampMs: number, limit: number): MonitorEvent[] {
    if (!Number.isFinite(limit) || limit <= 0 || !Number.isFinite(beforeTimestampMs)) return [];
    limit = Math.min(5000, Math.floor(limit));
    const result: MonitorEvent[] = [];
    let fd: number | undefined;
    try {
      fd = fs.openSync(this.filePath, "r");
      let position = fs.fstatSync(fd).size;
      const chunk = Buffer.alloc(64 * 1024);
      let carry: Buffer = Buffer.alloc(0);
      let skipOversizedLine = false;
      const parseLine = (line: Buffer) => {
        if (line.length === 0 || line.length > 1024 * 1024) return;
        try {
          const event = JSON.parse(line.toString("utf8")) as MonitorEvent;
          if (event && event.timestampMs < beforeTimestampMs) result.push(event);
        } catch { /* Ignore incomplete/corrupt JSONL records. */ }
      };
      while (position > 0 && result.length < limit) {
        const size = Math.min(chunk.length, position);
        position -= size;
        const bytes = fs.readSync(fd, chunk, 0, size, position);
        const data = Buffer.concat([chunk.subarray(0, bytes), carry]);
        let end = data.length;
        let newline: number;
        while (end > 0 && (newline = data.lastIndexOf(10, end - 1)) >= 0) {
          if (!skipOversizedLine) parseLine(data.subarray(newline + 1, end));
          skipOversizedLine = false;
          end = newline;
          if (result.length >= limit) break;
        }
        // Copy just the unfinished UTF-8 record; never retain the whole file.
        if (end > 1024 * 1024 || skipOversizedLine) {
          carry = Buffer.alloc(0);
          skipOversizedLine = true;
        } else {
          carry = Buffer.from(data.subarray(0, end));
        }
      }
      if (result.length < limit && !skipOversizedLine) parseLine(carry);
    } catch {
      // Missing/unavailable history must not stop live monitoring.
    } finally {
      if (fd !== undefined) fs.closeSync(fd);
    }
    return result.reverse();
  }
}
