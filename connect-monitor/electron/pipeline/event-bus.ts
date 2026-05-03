import type { MonitorEvent } from "./types";

export class MonitorEventBus {
  private readonly listeners: Array<(event: MonitorEvent) => void> = [];
  private readonly buffer: MonitorEvent[] = [];
  private readonly maxBufferSize: number;

  constructor(maxBufferSize = 4000) {
    this.maxBufferSize = maxBufferSize;
  }

  publish(event: MonitorEvent): void {
    this.buffer.push(event);
    if (this.buffer.length > this.maxBufferSize) {
      this.buffer.splice(0, this.buffer.length - this.maxBufferSize);
    }
    for (const listener of this.listeners) {
      listener(event);
    }
  }

  subscribe(handler: (event: MonitorEvent) => void): () => void {
    this.listeners.push(handler);
    return () => {
      const idx = this.listeners.indexOf(handler);
      if (idx >= 0) {
        this.listeners.splice(idx, 1);
      }
    };
  }

  snapshot(limit = 200): MonitorEvent[] {
    if (limit <= 0) {
      return [];
    }
    return this.buffer.slice(-limit);
  }

  clear(): void {
    this.buffer.splice(0, this.buffer.length);
  }
}
