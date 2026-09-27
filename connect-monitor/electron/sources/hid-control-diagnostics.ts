type Detail = Record<string, unknown>;

/** Bounded metadata only: no extra device reads and no I/O on the HID callback. */
export class HidControlDiagnostics {
  private nextId = 0;
  private operations = new Map<number, { name: string; startedAt: number }>();
  private lastLeaseCompletion = new Map<string, number>();
  private recent: Array<{ timestampMs: number; kind: string; detail: Detail }> = [];
  private leases: Record<string, { attempts: number; failures: number; lastWriteAt: number; lastDurationMs: number }> = {};

  record(kind: string, detail: Detail): void {
    this.recent.push({ timestampMs: Date.now(), kind, detail });
    if (this.recent.length > 64) this.recent.shift();
  }

  async io<T>(name: string, action: () => Promise<T>, lease?: string): Promise<T> {
    const id = ++this.nextId, startedAt = performance.now();
    this.operations.set(id, { name, startedAt });
    const counter = lease ? (this.leases[lease] ??= { attempts: 0, failures: 0, lastWriteAt: 0, lastDurationMs: 0 }) : null;
    if (counter) counter.attempts++;
    let error: string | undefined;
    try {
      const value = await action();
      // Native write completion is deliberately not called a firmware ACK.
      if (counter && lease) {
        const completedAt = performance.now(), previous = this.lastLeaseCompletion.get(lease);
        if (previous !== undefined && completedAt - previous > 1000)
          this.record("lease-write-gap", { lease, intervalMs: Math.round(completedAt - previous) });
        this.lastLeaseCompletion.set(lease, completedAt);
        counter.lastWriteAt = Date.now();
      }
      return value;
    } catch (reason) {
      error = String(reason).slice(0, 200);
      if (counter) counter.failures++;
      throw reason;
    } finally {
      const durationMs = Math.round(performance.now() - startedAt);
      if (counter) counter.lastDurationMs = durationMs;
      if (error || durationMs >= 250) this.record("control-io", { name, durationMs, error });
      this.operations.delete(id);
    }
  }

  snapshot() {
    const now = performance.now();
    return { leases: this.leases, operations: [...this.operations.values()].map(op => ({
      name: op.name, elapsedMs: Math.round(now - op.startedAt),
    })), recent: this.recent };
  }
}
