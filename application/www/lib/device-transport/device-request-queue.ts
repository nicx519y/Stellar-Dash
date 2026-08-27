import { DeviceRequestOptions } from './types';

type CommandParams = Record<string, unknown>;
type CommandResult = Record<string, unknown> | undefined;

type Waiter = {
  resolve: (value: unknown) => void;
  reject: (reason: Error) => void;
};

type QueueEntry = {
  id: number;
  key?: string;
  command: string;
  params: CommandParams;
  readyAt: number;
  deadlineAt: number;
  waiters: Waiter[];
  controller: AbortController;
  externalAbortCleanup?: () => void;
  cancelReason?: Error;
  cancelledFollowers?: QueueEntry[];
  settled: boolean;
  generation: number;
  run: (signal: AbortSignal) => Promise<unknown>;
};

type FlushWaiter = {
  resolve: () => void;
  reject: (reason: Error) => void;
  error: Error | null;
};

export type DeviceQueueSend = (
  command: string,
  params: CommandParams,
  options: DeviceRequestOptions,
) => Promise<CommandResult>;

export type DeviceQueueScheduleOptions = {
  coalescingKey?: string;
  mergeParams?: (previous: CommandParams, next: CommandParams) => CommandParams;
  debounceMs?: number;
  maxWaitMs?: number;
  timeoutMs?: number;
  signal?: AbortSignal;
};

/**
 * One ordered lane for every device operation. Explicitly coalesced writes
 * replace only a pending operation with the same resource key; an active write
 * is never mutated, so at most one follow-up commit carries the newest state.
 */
export class DeviceRequestQueue {
  private readonly pending: QueueEntry[] = [];
  private sendFunction: DeviceQueueSend | null = null;
  private active: QueueEntry | null = null;
  private timer: ReturnType<typeof setTimeout> | null = null;
  private generation = 0;
  private nextId = 1;
  private flushWaiters: FlushWaiter[] = [];

  constructor(private readonly defaultDebounceMs = 3_000) {}

  setSendFunction(send: DeviceQueueSend): void {
    this.sendFunction = send;
  }

  enqueue(
    command: string,
    params: CommandParams = {},
    immediate = false,
    options: DeviceQueueScheduleOptions = {},
  ): Promise<CommandResult> {
    const debounceMs = immediate
      ? 0
      : (options.debounceMs ?? this.defaultDebounceMs);
    const makeRun = (scheduledParams: CommandParams) => (signal: AbortSignal) => {
      if (!this.sendFunction) {
        throw new Error('Device request send function is not set');
      }
      return this.sendFunction(command, scheduledParams, {
        signal,
        timeoutMs: options.timeoutMs,
      });
    };
    return this.enqueueOperation<CommandResult>(
      command,
      makeRun(params),
      {
        ...options,
        debounceMs,
        maxWaitMs: immediate ? 0 : options.maxWaitMs,
      },
      params,
      makeRun,
    );
  }

  runExclusive<T>(
    label: string,
    operation: (signal: AbortSignal) => Promise<T>,
    options: Pick<DeviceQueueScheduleOptions, 'signal'> = {},
  ): Promise<T> {
    return this.enqueueOperation(label, operation, {
      debounceMs: 0,
      maxWaitMs: 0,
      signal: options.signal,
    });
  }

  sendPendingCommandImmediately(command: string): boolean {
    const now = Date.now();
    let found = false;
    for (const entry of this.pending) {
      if (entry.command !== command) continue;
      entry.readyAt = now;
      found = true;
    }
    if (found) this.reschedule();
    return found;
  }

  flushQueue(): Promise<void> {
    const now = Date.now();
    for (const entry of this.pending) entry.readyAt = now;
    if (!this.active && this.pending.length === 0) return Promise.resolve();
    const result = new Promise<void>((resolve, reject) => {
      this.flushWaiters.push({ resolve, reject, error: null });
    });
    this.reschedule();
    return result;
  }

  clear(reason = new Error('Device request queue cleared')): void {
    this.generation += 1;
    this.cancelTimer();
    const cancelledPending = this.pending.splice(0);
    for (const entry of cancelledPending) {
      entry.controller.abort(reason);
    }
    if (this.active) {
      const active = this.active;
      this.active = null;
      active.cancelReason = reason;
      active.cancelledFollowers = cancelledPending;
      active.controller.abort(reason);
    } else {
      cancelledPending.forEach((entry) => this.rejectEntry(entry, reason));
    }
    this.noteFlushError(reason);
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
      queueSize: this.pending.length,
      activeCommand: this.active?.command,
      queuedCommands: this.pending.map((entry) => entry.command),
    };
  }

  private enqueueOperation<T>(
    command: string,
    operation: (signal: AbortSignal) => Promise<T>,
    options: DeviceQueueScheduleOptions,
    params: CommandParams = {},
    operationFactory?: (
      params: CommandParams,
    ) => (signal: AbortSignal) => Promise<T>,
  ): Promise<T> {
    const now = Date.now();
    const debounceMs = Math.max(0, options.debounceMs ?? 0);
    const maxWaitMs = Math.max(debounceMs, options.maxWaitMs ?? debounceMs);
    const requestedReadyAt = now + debounceMs;
    const existingIndex = options.coalescingKey
      ? this.pending.findIndex((entry) => entry.key === options.coalescingKey)
      : -1;
    const existing = existingIndex >= 0
      ? this.pending[existingIndex]
      : undefined;

    const result = new Promise<T>((resolve, reject) => {
      if (existing) {
        const mergedParams = options.mergeParams
          ? options.mergeParams(existing.params, params)
          : params;
        existing.command = command;
        existing.params = mergedParams;
        existing.run = operationFactory
          ? operationFactory(mergedParams)
          : operation;
        existing.readyAt = Math.min(requestedReadyAt, existing.deadlineAt);
        existing.waiters.push({
          resolve: resolve as (value: unknown) => void,
          reject,
        });
        /*
         * The old pending snapshot is logically deleted. Re-append the same
         * entry at the tail with the latest payload so unrelated operations
         * that were queued afterwards retain their order. Keep deadlineAt
         * unchanged to guarantee convergence under continuous slider input.
         */
        this.pending.splice(existingIndex, 1);
        this.pending.push(existing);
        return;
      }

      const entry: QueueEntry = {
        id: this.nextId++,
        key: options.coalescingKey,
        command,
        params,
        readyAt: requestedReadyAt,
        deadlineAt: now + maxWaitMs,
        waiters: [{
          resolve: resolve as (value: unknown) => void,
          reject,
        }],
        controller: new AbortController(),
        settled: false,
        generation: this.generation,
        run: operation,
      };
      this.pending.push(entry);
      this.bindExternalAbort(entry, options.signal);
    });

    // An immediate operation is a causal barrier: every earlier debounced
    // mutation must commit first rather than allowing a stale read/action past.
    if (debounceMs === 0) {
      for (const entry of this.pending) {
        if (entry.readyAt > now) entry.readyAt = now;
      }
    }
    this.reschedule();
    return result;
  }

  private bindExternalAbort(entry: QueueEntry, signal?: AbortSignal): void {
    if (!signal) return;
    const abort = () => {
      if (entry.settled) return;
      const reason = signal.reason instanceof Error
        ? signal.reason
        : new Error('Device request aborted');
      entry.controller.abort(reason);
      if (this.active === entry) return;
      const index = this.pending.indexOf(entry);
      if (index >= 0) this.pending.splice(index, 1);
      this.rejectEntry(entry, reason);
      this.noteFlushError(reason);
      this.scheduleOrFinish();
    };
    if (signal.aborted) {
      abort();
    } else {
      signal.addEventListener('abort', abort, { once: true });
      entry.externalAbortCleanup = () => {
        signal.removeEventListener('abort', abort);
        entry.externalAbortCleanup = undefined;
      };
    }
  }

  private reschedule(): void {
    this.cancelTimer();
    this.schedule();
  }

  private schedule(): void {
    if (this.active || this.timer || this.pending.length === 0) return;
    const delay = Math.max(0, this.pending[0].readyAt - Date.now());
    this.timer = setTimeout(() => {
      this.timer = null;
      void this.drainOne();
    }, delay);
  }

  private async drainOne(): Promise<void> {
    if (this.active || this.pending.length === 0) return;
    const next = this.pending[0];
    if (next.readyAt > Date.now()) {
      this.schedule();
      return;
    }
    this.pending.shift();
    this.active = next;

    try {
      const result = await next.run(next.controller.signal);
      if (next.generation === this.generation) {
        this.resolveEntry(next, result);
      } else {
        const reason = next.cancelReason ?? new Error('Device request queue cleared');
        this.rejectEntry(next, reason);
        this.rejectCancelledFollowers(next, reason);
      }
    } catch (error) {
      const normalized = error instanceof Error ? error : new Error(String(error));
      this.rejectEntry(next, normalized);
      this.rejectCancelledFollowers(next, normalized);
      if (next.generation === this.generation) {
        this.noteFlushError(normalized);
      }
    } finally {
      if (this.active === next) this.active = null;
      this.scheduleOrFinish();
    }
  }

  private scheduleOrFinish(): void {
    if (!this.active && this.pending.length === 0) {
      this.finishFlush();
      return;
    }
    this.schedule();
  }

  private resolveEntry(entry: QueueEntry, value: unknown): void {
    if (entry.settled) return;
    entry.settled = true;
    entry.externalAbortCleanup?.();
    entry.waiters.forEach(({ resolve }) => resolve(value));
    entry.waiters = [];
  }

  private rejectEntry(entry: QueueEntry, error: Error): void {
    if (entry.settled) return;
    entry.settled = true;
    entry.externalAbortCleanup?.();
    entry.waiters.forEach(({ reject }) => reject(error));
    entry.waiters = [];
  }

  private noteFlushError(error: Error): void {
    for (const waiter of this.flushWaiters) {
      if (!waiter.error) waiter.error = error;
    }
  }

  private rejectCancelledFollowers(entry: QueueEntry, error: Error): void {
    const followers = entry.cancelledFollowers?.splice(0) ?? [];
    followers.forEach((follower) => this.rejectEntry(follower, error));
  }

  private finishFlush(): void {
    const waiters = this.flushWaiters.splice(0);
    for (const waiter of waiters) {
      if (waiter.error) waiter.reject(waiter.error);
      else waiter.resolve();
    }
  }

  private cancelTimer(): void {
    if (!this.timer) return;
    clearTimeout(this.timer);
    this.timer = null;
  }
}
