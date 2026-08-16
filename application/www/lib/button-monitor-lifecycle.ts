export interface ButtonMonitorReadiness {
  enabled: boolean;
  deviceConnected: boolean;
  dataIsReady: boolean;
  contextJsReady: boolean;
  layoutLength: number;
}

/**
 * Device monitoring is a ready-state resource. Opening it while startup data
 * is still loading races the final layout update and causes an unnecessary
 * stop/start pair when the layout length changes from zero.
 */
export function shouldStartButtonMonitoring(
  readiness: ButtonMonitorReadiness,
): boolean {
  return readiness.enabled
    && readiness.deviceConnected
    && readiness.dataIsReady
    && readiness.contextJsReady
    && readiness.layoutLength > 0;
}

export interface ButtonMonitorLifecycleOptions {
  startDevice: () => Promise<void>;
  stopDevice: () => Promise<void>;
  onAcquired?: () => void;
  onReleased?: () => void;
}

export interface SharedButtonMonitorLeaseToken {
  readonly generation: number;
  readonly id: number;
}

interface SharedButtonMonitorTransition {
  generation: number;
  promise: Promise<void>;
}

/**
 * Owns the single device-side button monitor for all mounted React consumers.
 *
 * Each successful acquire receives a generation-bound token. The first token
 * starts the device monitor and the final live token stops it. Tokens from a
 * disconnected session are harmless after beginSession()/endSession(), which
 * prevents delayed React cleanup from stopping a newly reconnected session.
 */
export class SharedButtonMonitorLease {
  private generation = 0;
  private active = false;
  private deviceActive = false;
  private nextTokenId = 1;
  private readonly tokens = new Set<SharedButtonMonitorLeaseToken>();
  private startInFlight: SharedButtonMonitorTransition | null = null;
  private stopInFlight: SharedButtonMonitorTransition | null = null;

  get ownerCount(): number {
    return this.tokens.size;
  }

  beginSession(): number {
    this.generation += 1;
    this.active = true;
    this.deviceActive = false;
    this.tokens.clear();
    this.startInFlight = null;
    this.stopInFlight = null;
    return this.generation;
  }

  endSession(): void {
    this.generation += 1;
    this.active = false;
    this.deviceActive = false;
    this.tokens.clear();
    this.startInFlight = null;
    this.stopInFlight = null;
  }

  acquire(startDevice: () => Promise<void>): Promise<SharedButtonMonitorLeaseToken> {
    return this.acquireForGeneration(this.generation, startDevice);
  }

  release(
    token: SharedButtonMonitorLeaseToken,
    stopDevice: () => Promise<void>,
  ): Promise<void> {
    const generation = this.generation;
    if (
      !this.isCurrent(generation)
      || token.generation !== generation
      || !this.tokens.delete(token)
    ) {
      return Promise.resolve();
    }

    if (this.tokens.size > 0 || !this.deviceActive) {
      return Promise.resolve();
    }

    // Ownership is cleared before the bounded stop. If its ACK is ambiguous,
    // session teardown remains the authoritative device-side cleanup.
    this.deviceActive = false;
    const transition: SharedButtonMonitorTransition = {
      generation,
      promise: Promise.resolve(),
    };
    transition.promise = Promise.resolve()
      .then(() => {
        this.assertCurrent(generation);
        return stopDevice();
      })
      .finally(() => {
        if (this.stopInFlight === transition) {
          this.stopInFlight = null;
        }
      });
    this.stopInFlight = transition;
    return transition.promise;
  }

  private acquireForGeneration(
    generation: number,
    startDevice: () => Promise<void>,
  ): Promise<SharedButtonMonitorLeaseToken> {
    try {
      this.assertCurrent(generation);
    } catch (error) {
      return Promise.reject(error);
    }

    const stopping = this.stopInFlight;
    if (stopping?.generation === generation) {
      return stopping.promise.then(() => this.acquireForGeneration(generation, startDevice));
    }

    if (this.deviceActive) {
      return Promise.resolve(this.issueToken(generation));
    }

    let starting = this.startInFlight;
    if (!starting || starting.generation !== generation) {
      const transition: SharedButtonMonitorTransition = {
        generation,
        promise: Promise.resolve(),
      };
      transition.promise = Promise.resolve()
        .then(() => {
          this.assertCurrent(generation);
          return startDevice();
        })
        .then(() => {
          this.assertCurrent(generation);
          this.deviceActive = true;
        })
        .finally(() => {
          if (this.startInFlight === transition) {
            this.startInFlight = null;
          }
        });
      this.startInFlight = transition;
      starting = transition;
    }

    return starting.promise.then(() => this.issueToken(generation));
  }

  private issueToken(generation: number): SharedButtonMonitorLeaseToken {
    this.assertCurrent(generation);
    const token = Object.freeze({
      generation,
      id: this.nextTokenId++,
    });
    this.tokens.add(token);
    return token;
  }

  private assertCurrent(generation: number): void {
    if (!this.isCurrent(generation)) {
      throw new Error('Button monitor session ended before the lease operation completed');
    }
  }

  private isCurrent(generation: number): boolean {
    return this.active && generation === this.generation;
  }
}

/**
 * Serializes one hook instance's monitoring ownership.
 *
 * A failed start never grants ownership, so a later render cleanup is a local
 * no-op instead of sending an unpaired stop_button_monitoring RPC. Clearing
 * ownership before stop also prevents cleanup retries after an ambiguous HID
 * timeout; WebHID session teardown owns the device-side fallback cleanup.
 */
export class ButtonMonitorLifecycle {
  private tail: Promise<void> = Promise.resolve();
  private ownsDeviceMonitor = false;
  private localMonitorActive = false;
  private disposed = false;
  private generation = 0;

  constructor(private readonly options: ButtonMonitorLifecycleOptions) {}

  get ownsMonitor(): boolean {
    return this.ownsDeviceMonitor;
  }

  /** React StrictMode may replay an effect after running its cleanup. */
  activate(): void {
    this.disposed = false;
  }

  start(): Promise<void> {
    const requestedGeneration = this.generation;
    return this.enqueue(async () => {
      if (
        this.disposed
        || requestedGeneration !== this.generation
        || this.ownsDeviceMonitor
      ) return;

      await this.options.startDevice();
      this.ownsDeviceMonitor = true;
      if (
        !this.disposed
        && requestedGeneration === this.generation
      ) {
        this.localMonitorActive = true;
        this.options.onAcquired?.();
      }
    });
  }

  stop(): Promise<void> {
    return this.enqueue(() => this.release());
  }

  dispose(): Promise<void> {
    this.generation += 1;
    this.disposed = true;
    return this.stop();
  }

  private async release(): Promise<void> {
    if (!this.ownsDeviceMonitor) return;

    this.ownsDeviceMonitor = false;
    if (this.localMonitorActive) {
      this.localMonitorActive = false;
      this.options.onReleased?.();
    }
    await this.options.stopDevice();
  }

  private enqueue(operation: () => Promise<void>): Promise<void> {
    const current = this.tail.then(operation, operation);
    this.tail = current.catch(() => undefined);
    return current;
  }
}
