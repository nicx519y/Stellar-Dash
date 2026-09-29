// One IPC batch may be in flight per consumer. A stalled renderer retains only
// the newest pending rows, rather than an unbounded Chromium message queue.
export class BoundedDelivery<T> {
  private pending: T[] = [];
  private inFlight: number | null = null;
  private sequence = 0;
  private dropped = 0;

  constructor(private readonly capacity = 2000, private readonly batchSize = 500) {}

  enqueue(items: readonly T[]): void {
    for (const item of items) this.pending.push(item);
    const overflow = this.pending.length - this.capacity;
    if (overflow > 0) {
      this.pending.splice(0, overflow);
      this.dropped += overflow;
    }
  }

  flush(send: (items: T[], sequence: number) => void): void {
    if (this.inFlight !== null || this.pending.length === 0) return;
    const batch = this.pending.splice(0, this.batchSize);
    this.inFlight = ++this.sequence;
    send(batch, this.inFlight);
  }

  acknowledge(sequence: number): void {
    if (this.inFlight === sequence) this.inFlight = null;
  }

  clear(): void {
    this.pending = [];
    // Keep the outstanding acknowledgement: clear must not open another IPC
    // slot while the consumer is still busy processing the previous batch.
  }

  reset(): void {
    this.clear();
    this.inFlight = null;
  }

  stats() {
    return { pending: this.pending.length, inFlight: this.inFlight !== null, dropped: this.dropped };
  }
}
