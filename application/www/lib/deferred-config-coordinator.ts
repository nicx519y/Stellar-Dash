export type DeferredConfigCommit = () => Promise<void>;
interface Entry { revision: number; priority: number; commit: DeferredConfigCommit }
export interface ConfigSyncState { pendingCount: number; saving: boolean; paused: boolean; error: string | null }
interface Options {
  autoFlush?: () => Promise<void>;
  onState?: (state: ConfigSyncState) => void;
  debounceMs?: number; maxWaitMs?: number; cooldownMs?: number;
}
/** Finite automatic batches; the physical queue still owns every HID exchange. */
export class DeferredConfigCoordinator {
  private entries = new Map<string, Entry>();
  private revision = 0;
  private generation = 0;
  private flight: Promise<void> | null = null;
  private automaticFlight = false;
  private timer: ReturnType<typeof setTimeout> | null = null;
  private firstEdit = 0;
  private lastEdit = 0;
  private lastCompleted = 0;
  private activeKey: string | null = null;
  private paused = false;
  private error: string | null = null;
  constructor(private readonly onDirtyChange?: (dirty: boolean) => void, private readonly options: Options = {}) {}
  get dirty(): boolean { return this.entries.size > 0; }
  get state(): ConfigSyncState { return { pendingCount: this.entries.size, saving: !!this.flight || this.automaticFlight, paused: this.paused, error: this.error }; }
  stage(key: string, commit: DeferredConfigCommit, priority = 0): void {
    if (!key) throw new Error('Deferred config resource key is required');
    if (!this.dirty) this.firstEdit = Date.now();
    this.lastEdit = Date.now();
    this.entries.set(key, { revision: ++this.revision, priority, commit });
    this.notify(); this.schedule();
  }
  remove(key: string): void {
    if (this.activeKey !== key) this.entries.delete(key);
    this.notify(); this.schedule();
  }
  pause(paused: boolean): void { this.paused = paused; this.notify(); this.schedule(); }
  retry(): void { this.error = null; this.notify(); this.schedule(); }
  clear(): void { this.generation++; this.entries.clear(); this.error = null; this.cancelTimer(); this.notify(); }
  /** Explicit boundaries drain all edits; automatic saves request a bounded round. */
  async flush(all = true): Promise<void> {
    this.cancelTimer();
    if (this.flight) { await this.flight; if (!all) return; }
    const generation = this.generation;
    do {
      if (generation !== this.generation) throw new Error('Configuration session ended');
      const operation = this.round(generation);
      this.flight = operation; this.notify();
      try { await operation; this.error = null; }
      catch (error) {
        if (generation === this.generation) this.error = error instanceof Error ? error.message : String(error);
        throw error;
      } finally {
        if (this.flight === operation) this.flight = null;
        this.notify(); this.schedule();
      }
    } while (all && this.dirty);
  }
  private async round(generation: number): Promise<void> {
    const keys = [...this.entries].sort(([ak, a], [bk, b]) => a.priority - b.priority || ak.localeCompare(bk)).map(([key]) => key);
    for (const key of keys) {
      const wait = Math.max(0, this.lastCompleted + (this.options.cooldownMs ?? 500) - Date.now());
      if (wait) await new Promise(resolve => setTimeout(resolve, wait));
      if (generation !== this.generation) throw new Error('Configuration session ended');
      const entry = this.entries.get(key);
      if (!entry) continue;
      if (key.startsWith('macros:') && this.entries.has(`profile:${key.slice(7)}`)) continue;
      if (key === 'selected-profile' && [...this.entries.keys()].some(k => k.startsWith('profile:') || k.startsWith('macros:'))) continue;
      this.activeKey = key;
      try { await entry.commit(); }
      finally { this.lastCompleted = Date.now(); this.activeKey = null; }
      if (generation !== this.generation) throw new Error('Configuration session ended');
      if (this.entries.get(key)?.revision === entry.revision) this.entries.delete(key);
      this.notify();
    }
  }
  private notify(): void { this.onDirtyChange?.(this.dirty); this.options.onState?.(this.state); }
  private cancelTimer(): void { if (this.timer) clearTimeout(this.timer); this.timer = null; }
  private schedule(): void {
    this.cancelTimer();
    if (!this.options.autoFlush || !this.dirty || this.paused || this.error || this.flight || this.automaticFlight) return;
    const due = Math.min(this.lastEdit + (this.options.debounceMs ?? 1000), this.firstEdit + (this.options.maxWaitMs ?? 5000));
    this.timer = setTimeout(() => {
      this.timer = null; this.automaticFlight = true; this.notify();
      const generation = this.generation;
      void this.options.autoFlush!().catch(error => {
        if (generation === this.generation) this.error = error instanceof Error ? error.message : String(error);
      }).finally(() => {
        this.automaticFlight = false;
        this.firstEdit = this.lastEdit || Date.now();
        this.notify(); this.schedule();
      });
    }, Math.max(0, due - Date.now()));
  }
}
