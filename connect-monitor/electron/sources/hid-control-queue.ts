type Job = { run: () => Promise<void>; queuedAt: number; name: string };

/** One native transfer at a time; reserve one slot for a coalesced renewal. */
export class HidControlQueue {
  private normal: Job[] = [];
  private renewal: Job | null = null;
  private active: { name: string; startedAt: number; waitMs: number } | null = null;
  private idleWaiters: Array<() => void> = [];

  enqueue<T>(name: string, action: () => Promise<T>, renewal = false): Promise<T> {
    if (renewal ? this.renewal !== null : this.normal.length >= 16)
      return Promise.reject(new Error("HID control queue full"));
    return new Promise<T>((resolve, reject) => {
      const job: Job = { name, queuedAt: performance.now(), run: async () => {
        try { resolve(await action()); } catch (error) { reject(error); }
      } };
      if (renewal) this.renewal = job;
      else this.normal.push(job);
      this.next();
    });
  }

  private next(): void {
    if (this.active) return;
    const job = this.renewal ?? this.normal.shift();
    this.renewal = null;
    if (!job) {
      for (const resolve of this.idleWaiters.splice(0)) resolve();
      return;
    }
    const now = performance.now();
    this.active = { name: job.name, startedAt: now, waitMs: Math.round(now - job.queuedAt) };
    void job.run().finally(() => { this.active = null; this.next(); });
  }

  idle(): Promise<void> {
    if (!this.active && !this.renewal && !this.normal.length) return Promise.resolve();
    return new Promise(resolve => this.idleWaiters.push(resolve));
  }

  stats() {
    return { pending: this.normal.length, renewalPending: this.renewal !== null,
      active: this.active && { name: this.active.name, waitMs: this.active.waitMs,
        elapsedMs: Math.round(performance.now() - this.active.startedAt) } };
  }
}
