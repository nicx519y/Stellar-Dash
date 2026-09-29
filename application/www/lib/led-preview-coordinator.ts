/** Keeps LED preview RPCs out of a persistent configuration transaction. */
export class LedPreviewCoordinator<T> {
  private snapshot: T | null = null;
  private suspension: symbol | null = null;

  get active(): boolean {
    return this.snapshot !== null;
  }

  push(snapshot: T, send: (snapshot: T) => Promise<void>): Promise<void> {
    this.snapshot = snapshot;
    return this.suspension ? Promise.resolve() : send(snapshot);
  }

  clear(send: () => Promise<void>): Promise<void> {
    this.snapshot = null;
    return send();
  }

  // The switch response is authoritative even before React renders the new
  // profile. Never restore the previous profile's colors after switching.
  replaceIfActive(snapshot: T): void {
    if (this.active) this.snapshot = snapshot;
  }

  async replay(send: (snapshot: T) => Promise<void>): Promise<void> {
    if (!this.suspension && this.snapshot !== null) await send(this.snapshot);
  }

  suspend(): symbol {
    const token = Symbol('LED preview suspension');
    this.suspension = token;
    return token;
  }

  async resume(
    token: symbol,
    send: (snapshot: T) => Promise<void>,
    restore = true,
  ): Promise<void> {
    if (this.suspension !== token) return;
    this.suspension = null;
    if (restore && this.snapshot !== null) await send(this.snapshot);
  }

  reset(): void {
    this.snapshot = null;
    this.suspension = null;
  }
}
