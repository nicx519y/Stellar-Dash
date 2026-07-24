import {
  DeviceTransport,
  DeviceTransportError,
  DeviceTransportState,
  Unsubscribe,
} from './types';

const DEVICE_CLOCK_MODULUS_US = 0x1_0000_0000;
const DEFAULT_SAMPLE_COUNT = 5;
export const DEVICE_CLOCK_SYNC_INTERVAL_MS = 10_000;

export interface DeviceClockSyncEstimate {
  /** Browser monotonic microseconds minus extended STM32 microseconds. */
  offsetUs: number;
  roundTripUs: number;
  deviceTimestampUs: number;
  extendedDeviceTimestampUs: number;
  browserMidpointUs: number;
  synchronizedAtBrowserTimeMs: number;
}

export interface DeviceClockSynchronizerOptions {
  sampleCount?: number;
  now?: () => number;
}

interface ClockSyncResponse {
  sampleId: number;
  deviceTimestampUs: number;
}

/**
 * Estimates the offset between performance.now() and STM32 monotonicMicros().
 *
 * Arrival time is used only to estimate this diagnostic clock mapping. Button
 * timing and performance calculations continue to consume device timestamps
 * directly and never substitute the browser's report-arrival time.
 */
export class DeviceClockSynchronizer {
  private readonly sampleCount: number;
  private readonly now: () => number;
  private nextSampleId = 1;
  private estimate: DeviceClockSyncEstimate | null = null;
  private inFlight: Promise<DeviceClockSyncEstimate> | null = null;

  constructor(
    private readonly transport: DeviceTransport,
    options: DeviceClockSynchronizerOptions = {},
  ) {
    this.sampleCount = options.sampleCount ?? DEFAULT_SAMPLE_COUNT;
    if (!Number.isInteger(this.sampleCount) || this.sampleCount < 3 || this.sampleCount > 15) {
      throw new RangeError('Clock synchronization sampleCount must be between 3 and 15');
    }
    this.now = options.now ?? (() => performance.now());
  }

  get current(): DeviceClockSyncEstimate | null {
    return this.estimate ? { ...this.estimate } : null;
  }

  synchronize(): Promise<DeviceClockSyncEstimate> {
    if (!this.inFlight) {
      this.inFlight = this.collectSamples().finally(() => {
        this.inFlight = null;
      });
    }
    return this.inFlight;
  }

  /**
   * Maps a wrapped STM32 u32 microsecond timestamp to the synchronized browser
   * monotonic timeline. This is intended for latency diagnostics and display,
   * not for calculating switch actuation timing.
   */
  deviceToBrowserTimeMs(deviceTimestampUs: number): number | null {
    if (!this.estimate || !isU32(deviceTimestampUs)) {
      return null;
    }
    const extended = unwrapDeviceTimestamp(
      deviceTimestampUs,
      this.estimate.extendedDeviceTimestampUs,
    );
    return (extended + this.estimate.offsetUs) / 1000;
  }

  private async collectSamples(): Promise<DeviceClockSyncEstimate> {
    const candidates: DeviceClockSyncEstimate[] = [];
    let referenceDeviceUs = this.estimate?.extendedDeviceTimestampUs ?? null;
    let lastError: unknown;

    for (let index = 0; index < this.sampleCount; index += 1) {
      const sampleId = this.allocateSampleId();
      const startMs = this.now();
      try {
        const response = await this.transport.request<ClockSyncResponse>(
          'performance.clock-sync',
          { sampleId },
        );
        const endMs = this.now();
        const value = response.data;
        if (
          !value ||
          value.sampleId !== sampleId ||
          !isU32(value.deviceTimestampUs) ||
          !Number.isFinite(startMs) ||
          !Number.isFinite(endMs) ||
          endMs < startMs
        ) {
          throw new DeviceTransportError(
            'protocol',
            'Device returned an invalid performance.clock-sync response',
          );
        }
        const midpointUs = ((startMs + endMs) / 2) * 1000;
        const predictedReference = this.estimate
          ? midpointUs - this.estimate.offsetUs
          : referenceDeviceUs;
        const extendedDeviceUs = unwrapDeviceTimestamp(
          value.deviceTimestampUs,
          predictedReference,
        );
        referenceDeviceUs = extendedDeviceUs;
        candidates.push({
          offsetUs: midpointUs - extendedDeviceUs,
          roundTripUs: (endMs - startMs) * 1000,
          deviceTimestampUs: value.deviceTimestampUs,
          extendedDeviceTimestampUs: extendedDeviceUs,
          browserMidpointUs: midpointUs,
          synchronizedAtBrowserTimeMs: endMs,
        });
      } catch (error) {
        lastError = error;
      }
    }

    if (candidates.length === 0) {
      throw lastError instanceof Error
        ? lastError
        : new DeviceTransportError('timeout', 'Unable to synchronize the STM32 clock');
    }
    candidates.sort((left, right) => left.roundTripUs - right.roundTripUs);
    this.estimate = candidates[0];
    return { ...this.estimate };
  }

  private allocateSampleId(): number {
    const value = this.nextSampleId;
    this.nextSampleId = value >= 0xffffffff ? 1 : value + 1;
    return value;
  }
}

export interface DeviceClockSyncSchedulerOptions {
  intervalMs?: number;
  setInterval?: typeof globalThis.setInterval;
  clearInterval?: typeof globalThis.clearInterval;
}

/**
 * Runs one clock synchronization when a WebHID session becomes connected and
 * repeats it every ten seconds while that authenticated session remains live.
 */
export class DeviceClockSyncScheduler {
  private readonly intervalMs: number;
  private readonly setIntervalImpl: typeof globalThis.setInterval;
  private readonly clearIntervalImpl: typeof globalThis.clearInterval;
  private timer: ReturnType<typeof setInterval> | null = null;
  private unsubscribe: Unsubscribe | null = null;

  constructor(
    private readonly transport: DeviceTransport,
    private readonly synchronizer: Pick<DeviceClockSynchronizer, 'synchronize'>,
    options: DeviceClockSyncSchedulerOptions = {},
  ) {
    this.intervalMs = options.intervalMs ?? DEVICE_CLOCK_SYNC_INTERVAL_MS;
    this.setIntervalImpl = options.setInterval ?? globalThis.setInterval.bind(globalThis);
    this.clearIntervalImpl = options.clearInterval ?? globalThis.clearInterval.bind(globalThis);
  }

  start(): void {
    if (this.transport.kind !== 'webhid' || this.timer !== null) {
      return;
    }
    this.unsubscribe = this.transport.onStateChange((state) => {
      if (state === DeviceTransportState.CONNECTED) {
        this.synchronize();
      }
    });
    if (this.transport.state === DeviceTransportState.CONNECTED) {
      this.synchronize();
    }
    this.timer = this.setIntervalImpl(() => {
      if (this.transport.state === DeviceTransportState.CONNECTED) {
        this.synchronize();
      }
    }, this.intervalMs);
  }

  stop(): void {
    this.unsubscribe?.();
    this.unsubscribe = null;
    if (this.timer !== null) {
      this.clearIntervalImpl(this.timer);
      this.timer = null;
    }
  }

  private synchronize(): void {
    void this.synchronizer.synchronize().catch(() => undefined);
  }
}

function isU32(value: unknown): value is number {
  return (
    typeof value === 'number' &&
    Number.isInteger(value) &&
    value >= 0 &&
    value <= 0xffffffff
  );
}

function unwrapDeviceTimestamp(value: number, reference: number | null): number {
  if (reference === null) {
    return value;
  }
  const epoch = Math.round((reference - value) / DEVICE_CLOCK_MODULUS_US);
  return value + epoch * DEVICE_CLOCK_MODULUS_US;
}
