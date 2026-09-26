export interface TelemetryCheckpointTicket {
  readonly generation: number;
  readonly signal: AbortSignal;
}

/**
 * Gives each started telemetry session one cancellation domain and guards
 * checkpoint bookkeeping from late promise callbacks belonging to an older
 * device session.
 */
export class TelemetryRequestSession {
  private generation = 0;
  private controller: AbortController | null = null;
  private checkpointTicket: TelemetryCheckpointTicket | null = null;

  get signal(): AbortSignal | null {
    return this.controller?.signal ?? null;
  }

  begin(): AbortSignal {
    this.generation += 1;
    this.controller?.abort();
    this.controller = new AbortController();
    this.checkpointTicket = null;
    return this.controller.signal;
  }

  end(): void {
    this.generation += 1;
    this.controller?.abort();
    this.controller = null;
    this.checkpointTicket = null;
  }

  beginCheckpoint(): TelemetryCheckpointTicket | null {
    const signal = this.controller?.signal;
    if (!signal || signal.aborted || this.checkpointTicket) return null;
    const ticket = Object.freeze({
      generation: this.generation,
      signal,
    });
    this.checkpointTicket = ticket;
    return ticket;
  }

  failCheckpoint(ticket: TelemetryCheckpointTicket): void {
    if (
      this.checkpointTicket === ticket
      && ticket.generation === this.generation
      && this.controller?.signal === ticket.signal
    ) {
      this.checkpointTicket = null;
    }
  }

  completeCheckpoint(): void {
    this.checkpointTicket = null;
  }
}
