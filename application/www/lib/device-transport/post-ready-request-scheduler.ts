/**
 * Tracks the initial batch scheduled by independent React effects after a
 * session becomes ready. Physical request serialization belongs exclusively to
 * DeviceCommandClient's DeviceRequestQueue; this gate only delays telemetry
 * until the initial batch has settled.
 */
export class PostReadyRequestScheduler {
  private generation = 0;
  private active = false;
  private initialBatchReleased = false;
  private releaseDrainActive = false;
  private releaseCallback: (() => void) | null = null;
  private readonly initialTasks = new Set<Promise<unknown>>();

  beginSession(): number {
    this.generation += 1;
    this.active = true;
    this.initialBatchReleased = false;
    this.releaseDrainActive = false;
    this.releaseCallback = null;
    this.initialTasks.clear();
    return this.generation;
  }

  endSession(): void {
    this.generation += 1;
    this.active = false;
    this.initialBatchReleased = false;
    this.releaseDrainActive = false;
    this.releaseCallback = null;
    this.initialTasks.clear();
  }

  /**
   * Register one bounded device operation. DeviceCommandClient preserves its
   * order on the shared device lane; this wrapper only enforces generation.
   */
  schedule<T>(operation: () => Promise<T>): Promise<T> {
    if (!this.active) {
      return operation();
    }

    const generation = this.generation;
    const run = async (): Promise<T> => {
      if (!this.isCurrent(generation)) {
        throw new Error('Post-ready device session ended before the request started');
      }
      return operation();
    };
    const current = Promise.resolve().then(run);
    return this.track(current);
  }

  /**
   * Track a composite task such as calibration-check -> monitor-start. This
   * keeps the initial gate closed while the task schedules its next RPC.
   */
  track<T>(task: Promise<T>): Promise<T> {
    if (!this.active || this.initialBatchReleased) {
      return task;
    }

    const generation = this.generation;
    this.initialTasks.add(task);
    void task.then(
      () => this.finishInitialTask(task, generation),
      () => this.finishInitialTask(task, generation),
    );
    return task;
  }

  /**
   * Release background telemetry after the initial React effects have
   * registered their critical work and every registered task has settled.
   * Microtask draining is deterministic and adds no time-based delay.
   */
  releaseInitialBatchWhenIdle(callback: () => void): void {
    if (!this.active || this.initialBatchReleased) return;
    this.releaseCallback = callback;
    if (this.releaseDrainActive) return;

    this.releaseDrainActive = true;
    const generation = this.generation;
    void this.drainInitialBatch(generation);
  }

  private async drainInitialBatch(generation: number): Promise<void> {
    // Let every effect from the READY render register its synchronous task.
    await Promise.resolve();

    while (this.isCurrent(generation) && !this.initialBatchReleased) {
      const batch = [...this.initialTasks];
      if (batch.length > 0) {
        await Promise.allSettled(batch);
        // A composite task may enqueue its final RPC in its continuation.
        await Promise.resolve();
        continue;
      }

      // Require one stable empty microtask turn before releasing telemetry.
      await Promise.resolve();
      if (this.initialTasks.size > 0) continue;
      if (!this.isCurrent(generation)) return;

      this.initialBatchReleased = true;
      this.releaseDrainActive = false;
      const callback = this.releaseCallback;
      this.releaseCallback = null;
      callback?.();
      return;
    }
  }

  private finishInitialTask(task: Promise<unknown>, generation: number): void {
    if (!this.isCurrent(generation)) return;
    this.initialTasks.delete(task);
  }

  private isCurrent(generation: number): boolean {
    return this.active && generation === this.generation;
  }
}
