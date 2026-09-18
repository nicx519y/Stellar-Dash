import fs from "node:fs";
import path from "node:path";

import type { MonitorEvent } from "./types";

export class MonitorEventStore {
  private readonly filePath: string;

  constructor(baseDir: string) {
    this.filePath = path.join(baseDir, "monitor-events.jsonl");
    fs.mkdirSync(path.dirname(this.filePath), { recursive: true });
  }

  append(events: MonitorEvent[]): void {
    if (events.length === 0) return;
    const data = events.map((event) => JSON.stringify(event)).join("\n") + "\n";
    fs.appendFileSync(this.filePath, data);
  }

  clear(): void {
    try {
      fs.rmSync(this.filePath, { force: true });
    } catch {
    }
  }

  queryBefore(beforeTimestampMs: number, limit: number): MonitorEvent[] {
    if (limit <= 0 || !Number.isFinite(beforeTimestampMs)) return [];
    let content = "";
    try {
      content = fs.readFileSync(this.filePath, "utf8");
    } catch {
      return [];
    }

    const result: MonitorEvent[] = [];
    const lines = content.trimEnd().split("\n");
    for (let i = lines.length - 1; i >= 0 && result.length < limit; i--) {
      try {
        const event = JSON.parse(lines[i]) as MonitorEvent;
        if (event.timestampMs < beforeTimestampMs) {
          result.push(event);
        }
      } catch {
      }
    }
    return result.reverse();
  }
}
