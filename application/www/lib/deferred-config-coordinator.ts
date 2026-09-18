export type DeferredConfigCommit = () => Promise<void>;

interface DeferredConfigEntry {
  revision: number;
  priority: number;
  commit: DeferredConfigCommit;
}

/**
 * Holds durable device writes until an explicit configuration boundary.
 *
 * Staging the same resource replaces the older snapshot. A failed commit is
 * deliberately retained so navigation/reboot can be blocked and retried.
 */
export class DeferredConfigCoordinator {
  private readonly entries = new Map<string, DeferredConfigEntry>();
  private nextRevision = 1;
  private flushInFlight: Promise<void> | null = null;

  constructor(private readonly onDirtyChange?: (dirty: boolean) => void) {}

  get dirty(): boolean {
    return this.entries.size > 0;
  }

  stage(
    resourceKey: string,
    commit: DeferredConfigCommit,
    priority = 0,
  ): void {
    if (!resourceKey) throw new Error('Deferred config resource key is required');
    const wasDirty = this.dirty;
    this.entries.set(resourceKey, {
      revision: this.nextRevision++,
      priority,
      commit,
    });
    if (!wasDirty) this.onDirtyChange?.(true);
  }

  clear(): void {
    const wasDirty = this.dirty;
    this.entries.clear();
    if (wasDirty) this.onDirtyChange?.(false);
  }

  flush(): Promise<void> {
    if (this.flushInFlight) return this.flushInFlight;

    const operation = this.flushUntilClean();
    this.flushInFlight = operation;
    void operation.finally(() => {
      if (this.flushInFlight === operation) this.flushInFlight = null;
    }).catch(() => undefined);
    return operation;
  }

  private async flushUntilClean(): Promise<void> {
    while (this.entries.size > 0) {
      const snapshot = Array.from(this.entries.entries()).sort(
        ([leftKey, left], [rightKey, right]) => (
          left.priority - right.priority || leftKey.localeCompare(rightKey)
        ),
      );

      for (const [resourceKey, entry] of snapshot) {
        await entry.commit();
        if (this.entries.get(resourceKey)?.revision === entry.revision) {
          this.entries.delete(resourceKey);
        }
      }
    }
    this.onDirtyChange?.(false);
  }
}
