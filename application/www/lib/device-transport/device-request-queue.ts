import { DeviceRequestOptions } from './types';

type CommandParams = Record<string, unknown>;
type CommandResult = Record<string, unknown> | undefined;

type Waiter = {
  resolve: (value: CommandResult) => void;
  reject: (reason: Error) => void;
};

type QueueEntry = {
  key: string;
  command: string;
  params: CommandParams;
  canSendAt: number;
  waiters: Waiter[];
  controller: AbortController;
  settled: boolean;
  generation: number;
};

export type DeviceQueueSend = (
  command: string,
  params: CommandParams,
  options: DeviceRequestOptions,
) => Promise<CommandResult>;

/**
 * Coalesces explicitly debounced writes while keeping the active operation
 * observable and cancellable. Reads should normally call DeviceCommandClient
 * directly instead of entering this queue.
 */
export class DeviceRequestQueue {
  private readonly queued = new Map<string, QueueEntry>();
  private sendFunction: DeviceQueueSend | null = null;
  private active: QueueEntry | null = null;
  private timer: ReturnType<typeof setTimeout> | null = null;
  private generation = 0;
  private flushWaiters: Array<() => void> = [];

  constructor(private readonly debounceMs = 3_000) {}

  setSendFunction(send: DeviceQueueSend): void {
    this.sendFunction = send;
  }

  enqueue(
    command: string,
    params: CommandParams = {},
    immediate = false,
  ): Promise<CommandResult> {
    return new Promise((resolve, reject) => {
      const canSendAt = immediate ? Date.now() : Date.now() + this.debounceMs;
      const key = this.coalescingKey(command, params);
      const existing = this.queued.get(key);
      if (existing) {
        existing.params = params;
        existing.canSendAt = canSendAt;
        existing.waiters.push({ resolve, reject });
      } else {
        this.queued.set(key, {
          key,
          command,
          params,
          canSendAt,
          waiters: [{ resolve, reject }],
          controller: new AbortController(),
          settled: false,
          generation: this.generation,
        });
      }
      this.schedule();
    });
  }

  sendPendingCommandImmediately(command: string): boolean {
    let found = false;
    for (const entry of this.queued.values()) {
      if (entry.command !== command) continue;
      entry.canSendAt = Date.now();
      found = true;
    }
    if (!found) return false;
    this.schedule();
    return true;
  }

  flushQueue(): Promise<void> {
    for (const entry of this.queued.values()) {
      entry.canSendAt = Date.now();
    }
    if (!this.active && this.queued.size === 0) return Promise.resolve();
    const result = new Promise<void>((resolve) => this.flushWaiters.push(resolve));
    this.schedule();
    return result;
  }

  clear(reason = new Error('Device request queue cleared')): void {
    this.generation += 1;
    this.cancelTimer();
    for (const entry of this.queued.values()) {
      entry.controller.abort(reason);
      this.rejectEntry(entry, reason);
    }
    this.queued.clear();
    if (this.active) {
      const active = this.active;
      this.active = null;
      active.controller.abort(reason);
      this.rejectEntry(active, reason);
    }
    this.finishFlush();
  }

  destroy(): void {
    this.clear(new Error('Device request queue destroyed'));
    this.sendFunction = null;
  }

  getStatus(): {
    queueSize: number;
    activeCommand?: string;
    queuedCommands: string[];
  } {
    return {
      queueSize: this.queued.size,
      activeCommand: this.active?.command,
      queuedCommands: [...this.queued.values()].map((entry) => entry.command),
    };
  }

  private schedule(): void {
    if (this.active || this.timer || this.queued.size === 0) return;
    let earliest = Number.POSITIVE_INFINITY;
    for (const entry of this.queued.values()) {
      earliest = Math.min(earliest, entry.canSendAt);
    }
    const delay = Math.max(0, earliest - Date.now());
    this.timer = setTimeout(() => {
      this.timer = null;
      void this.drainOne();
    }, delay);
  }

  private async drainOne(): Promise<void> {
    if (this.active) return;
    const now = Date.now();
    let next: QueueEntry | null = null;
    for (const entry of this.queued.values()) {
      if (entry.canSendAt <= now && (!next || entry.canSendAt < next.canSendAt)) {
        next = entry;
      }
    }
    if (!next) {
      this.schedule();
      return;
    }
    this.queued.delete(next.key);
    this.active = next;
    const send = this.sendFunction;
    if (!send) {
      this.active = null;
      this.rejectEntry(next, new Error('Device request send function is not set'));
      this.scheduleOrFinish();
      return;
    }

    try {
      const result = await send(next.command, next.params, {
        signal: next.controller.signal,
      });
      if (next.generation === this.generation) this.resolveEntry(next, result);
    } catch (error) {
      const normalized = error instanceof Error ? error : new Error(String(error));
      if (next.generation === this.generation) this.rejectEntry(next, normalized);
    } finally {
      if (this.active === next) this.active = null;
      this.scheduleOrFinish();
    }
  }

  private scheduleOrFinish(): void {
    if (!this.active && this.queued.size === 0) {
      this.finishFlush();
      return;
    }
    this.schedule();
  }

  private resolveEntry(entry: QueueEntry, value: CommandResult): void {
    if (entry.settled) return;
    entry.settled = true;
    entry.waiters.forEach(({ resolve }) => resolve(value));
    entry.waiters = [];
  }

  private rejectEntry(entry: QueueEntry, error: Error): void {
    if (entry.settled) return;
    entry.settled = true;
    entry.waiters.forEach(({ reject }) => reject(error));
    entry.waiters = [];
  }

  private finishFlush(): void {
    const waiters = this.flushWaiters.splice(0);
    waiters.forEach((resolve) => resolve());
  }

  private cancelTimer(): void {
    if (!this.timer) return;
    clearTimeout(this.timer);
    this.timer = null;
  }

  private coalescingKey(command: string, params: CommandParams): string {
    if (command === 'update_profile') {
      const details = params.profileDetails;
      const detailsId = details && typeof details === 'object' && !Array.isArray(details)
        ? (details as Record<string, unknown>).id
        : undefined;
      const profileId = typeof params.profileId === 'string'
        ? params.profileId
        : (typeof detailsId === 'string' ? detailsId : '');
      return `${command}:${profileId}`;
    }
    return command;
  }
}
