import {
  ButtonPerformanceMonitoringBinaryData,
} from '../button-performance-binary-parser';
import {
  DeviceEvent,
  DeviceTransport,
  Unsubscribe,
} from './types';
import {
  DeviceClockSyncRequester,
  DeviceClockSyncEstimate,
  DeviceClockSyncScheduler,
  DeviceClockSynchronizer,
} from './device-clock-sync';
import {
  PerformanceCheckpoint,
  PerformanceCheckpointAssembler,
  PerformanceEdge,
  PerformanceSample,
  PerformanceTelemetryCache,
  applyCheckpointPreservingEdges,
  parsePerformanceCheckpointChunk,
  parsePerformanceEdge,
  parsePerformanceSample,
} from './performance-codec';
import { TelemetryRequestSession } from './telemetry-request-session';

const EDGE_REPLAY_CAPACITY = 64;

export interface PerformanceTelemetryControllerOptions {
  clockSynchronizer?: DeviceClockSynchronizer;
  clockSyncIntervalMs?: number;
  requester?: DeviceClockSyncRequester;
}

export interface PerformanceTelemetryStartOptions {
  deferClockSync?: boolean;
}

/**
 * Parses telemetry in a worker where available and coalesces 100 Hz input into
 * at most one React-facing update per animation frame.
 */
export class PerformanceTelemetryController {
  private readonly cache = new PerformanceTelemetryCache();
  private readonly checkpointAssembler = new PerformanceCheckpointAssembler();
  private readonly clockSynchronizer: DeviceClockSynchronizer;
  private readonly clockSyncScheduler: DeviceClockSyncScheduler;
  private readonly requester: DeviceClockSyncRequester;
  private readonly handlers = new Set<(snapshot: ButtonPerformanceMonitoringBinaryData) => void>();
  private readonly unsubscribers: Unsubscribe[] = [];
  private readonly recentEdges: PerformanceEdge[] = [];
  private worker: Worker | null = null;
  private renderScheduled = false;
  private lastEdgeSequence: number | null = null;
  private edgesDuringCheckpoint = 0;
  private readonly requestSession = new TelemetryRequestSession();

  constructor(
    private readonly transport: DeviceTransport,
    options: PerformanceTelemetryControllerOptions = {},
  ) {
    this.requester = options.requester ?? transport;
    this.clockSynchronizer =
      options.clockSynchronizer ?? new DeviceClockSynchronizer(this.requester);
    this.clockSyncScheduler = new DeviceClockSyncScheduler(
      transport,
      this.clockSynchronizer,
      { intervalMs: options.clockSyncIntervalMs },
    );
  }

  start(options: PerformanceTelemetryStartOptions = {}): void {
    if (this.unsubscribers.length > 0) {
      if (!options.deferClockSync) this.startClockSync();
      return;
    }
    this.requestSession.begin();
    if (typeof Worker !== 'undefined') {
      this.worker = new Worker(new URL('./performance-worker.ts', import.meta.url), {
        type: 'module',
        name: 'hbox-performance-telemetry',
      });
      this.worker.onmessage = (event: MessageEvent<WorkerResult>) => {
        if (event.data.kind === 'sample') this.cache.applySample(event.data.value);
        if (event.data.kind === 'edge') this.applyEdge(event.data.value);
        if (event.data.kind === 'error') {
          this.requestCheckpoint();
          return;
        }
        this.scheduleRender();
      };
    }
    this.unsubscribers.push(
      this.transport.subscribe<Uint8Array>('performance.sample', (event) => {
        this.parseOrPost('sample', event);
      }),
      this.transport.subscribe<Uint8Array>('performance.edge', (event) => {
        this.parseOrPost('edge', event);
      }),
      this.transport.subscribe<Uint8Array>('performance.checkpoint', (event) => {
        this.applyCheckpointChunk(event);
      }),
      this.transport.subscribe('transport.sequence-gap', () => {
        this.checkpointAssembler.reset(true);
        this.requestCheckpoint();
      }),
    );
    if (!options.deferClockSync) this.startClockSync();
  }

  startClockSync(): void {
    const signal = this.requestSession.signal;
    if (this.unsubscribers.length === 0 || !signal || signal.aborted) return;
    this.clockSyncScheduler.start(signal);
  }

  /** Pause only periodic browser-to-device probes without aborting an in-flight request. */
  pauseClockSync(): void {
    this.clockSyncScheduler.stop();
  }

  stop(): void {
    this.requestSession.end();
    this.clockSyncScheduler.stop();
    this.unsubscribers.splice(0).forEach((unsubscribe) => unsubscribe());
    this.worker?.terminate();
    this.worker = null;
    this.renderScheduled = false;
    this.checkpointAssembler.reset(true);
    this.recentEdges.length = 0;
    this.edgesDuringCheckpoint = 0;
    this.lastEdgeSequence = null;
  }

  subscribe(handler: (snapshot: ButtonPerformanceMonitoringBinaryData) => void): Unsubscribe {
    this.handlers.add(handler);
    return () => this.handlers.delete(handler);
  }

  getClockSyncEstimate(): DeviceClockSyncEstimate | null {
    return this.clockSynchronizer.current;
  }

  /** Start a UI test with no samples, edges, or checkpoint history from the
   * previous test run. Transport subscriptions and clock sync stay alive. */
  resetMonitoringSession(): void {
    this.cache.reset();
    this.checkpointAssembler.reset(true);
    this.recentEdges.length = 0;
    this.edgesDuringCheckpoint = 0;
    this.lastEdgeSequence = null;
    this.requestSession.completeCheckpoint();
  }

  private parseOrPost(kind: 'sample' | 'edge', event: DeviceEvent<Uint8Array>): void {
    const bytes = event.data.slice();
    if (this.worker) {
      this.worker.postMessage({ kind, payload: bytes.buffer }, [bytes.buffer]);
      return;
    }
    if (kind === 'sample') this.cache.applySample(parsePerformanceSample(bytes));
    else this.applyEdge(parsePerformanceEdge(bytes));
    this.scheduleRender();
  }

  private applyEdge(edge: PerformanceEdge): void {
    if (
      this.lastEdgeSequence !== null &&
      edge.edgeSequence !== ((this.lastEdgeSequence + 1) >>> 0)
    ) {
      this.checkpointAssembler.reset(true);
      this.edgesDuringCheckpoint = 0;
      this.requestCheckpoint();
    }
    this.lastEdgeSequence = edge.edgeSequence;
    this.recentEdges.push(edge);
    if (this.recentEdges.length > EDGE_REPLAY_CAPACITY) {
      this.recentEdges.shift();
    }
    const checkpointBaseline = this.checkpointAssembler.baselineEdgeSequence;
    if (
      checkpointBaseline !== null &&
      sequenceIsAfter(edge.edgeSequence, checkpointBaseline)
    ) {
      this.edgesDuringCheckpoint += 1;
      if (this.edgesDuringCheckpoint > EDGE_REPLAY_CAPACITY) {
        this.checkpointAssembler.reset(true);
        this.edgesDuringCheckpoint = 0;
        this.requestCheckpoint();
      }
    }
    this.cache.applyEdge(edge);
  }

  private applyCheckpointChunk(event: DeviceEvent<Uint8Array>): void {
    try {
      const chunk = parsePerformanceCheckpointChunk(event.data);
      const wasActive = this.checkpointAssembler.active;
      const checkpoint = this.checkpointAssembler.push(chunk);
      if (!wasActive && chunk.chunkIndex === 0) {
        this.edgesDuringCheckpoint = 0;
      }
      if (!checkpoint) {
        return;
      }
      this.applyCheckpointWithoutRollback(checkpoint);
      this.edgesDuringCheckpoint = 0;
      this.requestSession.completeCheckpoint();
      this.scheduleRender();
    } catch {
      this.checkpointAssembler.reset(true);
      this.edgesDuringCheckpoint = 0;
      this.requestCheckpoint();
    }
  }

  private applyCheckpointWithoutRollback(checkpoint: PerformanceCheckpoint): void {
    this.lastEdgeSequence = applyCheckpointPreservingEdges(
      this.cache,
      checkpoint,
      this.recentEdges,
    );
  }

  private scheduleRender(): void {
    if (this.renderScheduled) return;
    this.renderScheduled = true;
    const schedule = typeof requestAnimationFrame === 'function'
      ? requestAnimationFrame
      : (callback: FrameRequestCallback) => setTimeout(() => callback(Date.now()), 16) as unknown as number;
    schedule(() => {
      this.renderScheduled = false;
      const snapshot = this.cache.snapshot();
      this.handlers.forEach((handler) => handler(snapshot));
    });
  }

  private requestCheckpoint(): void {
    const ticket = this.requestSession.beginCheckpoint();
    if (!ticket) return;
    void this.requester.request(
      'performance.get-checkpoint',
      {},
      { signal: ticket.signal },
    ).catch(() => {
      this.requestSession.failCheckpoint(ticket);
    });
  }

}

type WorkerResult =
  | { kind: 'sample'; value: PerformanceSample }
  | { kind: 'edge'; value: PerformanceEdge }
  | { kind: 'error'; message: string };

function sequenceIsAfter(value: number, baseline: number): boolean {
  const distance = (value - baseline) >>> 0;
  return distance !== 0 && distance < 0x8000_0000;
}
