import {
  ButtonPerformanceData,
  ButtonPerformanceMonitoringBinaryData,
  BUTTON_PERFORMANCE_MONITORING_CMD,
} from '../button-performance-binary-parser';

export const PERFORMANCE_BUTTON_COUNT = 18;
export const PERF_SAMPLE_PAYLOAD_SIZE = 44;
export const PERF_EDGE_PAYLOAD_SIZE = 22;
export const PERF_CHECKPOINT_CHUNK_PAYLOAD_SIZE = 44;
export const PERF_CHECKPOINT_CHUNK_COUNT = 9;
export const PERF_CHECKPOINT_BUTTONS_PER_CHUNK = 2;

export interface PerformanceSample {
  deviceTimestampUs: number;
  pressedMask: number;
  droppedSamples: number;
  currentDistanceUm: readonly number[];
}

export interface PerformanceEdge {
  deviceTimestampUs: number;
  edgeSequence: number;
  buttonIndex: number;
  pressed: boolean;
  rawAdc: number;
  currentDistanceUm: number;
  pressTriggerDistanceUm: number;
  pressStartDistanceUm: number;
  releaseTriggerDistanceUm: number;
  releaseStartDistanceUm: number;
}

export interface PerformanceCheckpoint {
  deviceTimestampUs: number;
  edgeSequence?: number;
  maxTravelDistanceUm: number;
  droppedSamples: number;
  buttons: Array<{
    buttonIndex: number;
    virtualPin: number;
    pressed: boolean;
    rawAdc?: number;
    currentDistanceUm: number;
    pressTriggerDistanceUm: number;
    pressStartDistanceUm: number;
    releaseTriggerDistanceUm: number;
    releaseStartDistanceUm: number;
  }>;
}

export interface PerformanceCheckpointChunk {
  deviceTimestampUs: number;
  edgeSequence: number;
  maxTravelDistanceUm: number;
  droppedSamples: number;
  checkpointId: number;
  chunkIndex: number;
  chunkCount: number;
  firstButton: number;
  buttons: PerformanceCheckpoint['buttons'];
}

export function parsePerformanceSample(payload: ArrayBuffer | Uint8Array): PerformanceSample {
  const bytes = exactBytes(payload);
  if (bytes.byteLength !== PERF_SAMPLE_PAYLOAD_SIZE) {
    throw new Error(`PERF_SAMPLE must be exactly ${PERF_SAMPLE_PAYLOAD_SIZE} bytes`);
  }
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const currentDistanceUm: number[] = [];
  for (let index = 0; index < PERFORMANCE_BUTTON_COUNT; index += 1) {
    currentDistanceUm.push(view.getUint16(8 + index * 2, true));
  }
  return {
    deviceTimestampUs: view.getUint32(0, true),
    pressedMask: bytes[4] | (bytes[5] << 8) | (bytes[6] << 16),
    droppedSamples: bytes[7],
    currentDistanceUm,
  };
}

export function parsePerformanceEdge(payload: ArrayBuffer | Uint8Array): PerformanceEdge {
  const bytes = exactBytes(payload);
  if (bytes.byteLength !== PERF_EDGE_PAYLOAD_SIZE) {
    throw new Error(`PERF_EDGE must be exactly ${PERF_EDGE_PAYLOAD_SIZE} bytes`);
  }
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  return {
    deviceTimestampUs: view.getUint32(0, true),
    edgeSequence: view.getUint32(4, true),
    buttonIndex: bytes[8],
    pressed: bytes[9] !== 0,
    rawAdc: view.getUint16(10, true),
    currentDistanceUm: view.getUint16(12, true),
    pressTriggerDistanceUm: view.getUint16(14, true),
    pressStartDistanceUm: view.getUint16(16, true),
    releaseTriggerDistanceUm: view.getUint16(18, true),
    releaseStartDistanceUm: view.getUint16(20, true),
  };
}

export function parsePerformanceCheckpointChunk(
  payload: ArrayBuffer | Uint8Array,
): PerformanceCheckpointChunk {
  const bytes = exactBytes(payload);
  if (bytes.byteLength !== PERF_CHECKPOINT_CHUNK_PAYLOAD_SIZE) {
    throw new Error(
      `PERF_CHECKPOINT chunk must be exactly ${PERF_CHECKPOINT_CHUNK_PAYLOAD_SIZE} bytes`,
    );
  }
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const chunkIndex = bytes[13];
  const chunkCount = bytes[14];
  const firstButton = bytes[15];
  if (
    bytes[12] === 0 ||
    chunkCount !== PERF_CHECKPOINT_CHUNK_COUNT ||
    chunkIndex >= chunkCount ||
    firstButton !== chunkIndex * PERF_CHECKPOINT_BUTTONS_PER_CHUNK ||
    firstButton + PERF_CHECKPOINT_BUTTONS_PER_CHUNK > PERFORMANCE_BUTTON_COUNT
  ) {
    throw new Error('PERF_CHECKPOINT chunk header is inconsistent');
  }

  const buttons: PerformanceCheckpoint['buttons'] = [];
  for (let record = 0; record < PERF_CHECKPOINT_BUTTONS_PER_CHUNK; record += 1) {
    const offset = 16 + record * 14;
    const flags = bytes[offset + 1];
    if ((flags & ~0x01) !== 0) {
      throw new Error('PERF_CHECKPOINT button flags contain reserved bits');
    }
    buttons.push({
      buttonIndex: firstButton + record,
      virtualPin: bytes[offset],
      pressed: (flags & 0x01) !== 0,
      rawAdc: view.getUint16(offset + 2, true),
      currentDistanceUm: view.getUint16(offset + 4, true),
      pressTriggerDistanceUm: view.getUint16(offset + 6, true),
      pressStartDistanceUm: view.getUint16(offset + 8, true),
      releaseTriggerDistanceUm: view.getUint16(offset + 10, true),
      releaseStartDistanceUm: view.getUint16(offset + 12, true),
    });
  }

  return {
    deviceTimestampUs: view.getUint32(0, true),
    edgeSequence: view.getUint32(4, true),
    maxTravelDistanceUm: view.getUint16(8, true),
    droppedSamples: view.getUint16(10, true),
    checkpointId: bytes[12],
    chunkIndex,
    chunkCount,
    firstButton,
    buttons,
  };
}

/**
 * Reassembles exactly nine ordered, fixed-size checkpoint reports. A new ID,
 * skipped/duplicate chunk, or inconsistent baseline is rejected instead of
 * combining snapshots from different points in device time.
 */
export class PerformanceCheckpointAssembler {
  private activeId: number | null = null;
  private expectedChunk = 0;
  private baseline: Omit<PerformanceCheckpoint, 'buttons'> | null = null;
  private readonly buttons: PerformanceCheckpoint['buttons'] = [];
  private lastCompletedId: number | null = null;

  get active(): boolean {
    return this.activeId !== null;
  }

  get baselineEdgeSequence(): number | null {
    return this.baseline?.edgeSequence ?? null;
  }

  push(chunk: PerformanceCheckpointChunk): PerformanceCheckpoint | null {
    if (this.activeId === null) {
      if (chunk.chunkIndex !== 0) {
        throw new Error('PERF_CHECKPOINT did not start at chunk zero');
      }
      if (
        this.lastCompletedId !== null &&
        chunk.checkpointId !== nextCheckpointId(this.lastCompletedId)
      ) {
        throw new Error('PERF_CHECKPOINT id gap detected');
      }
      this.activeId = chunk.checkpointId;
      this.expectedChunk = 0;
      this.baseline = {
        deviceTimestampUs: chunk.deviceTimestampUs,
        edgeSequence: chunk.edgeSequence,
        maxTravelDistanceUm: chunk.maxTravelDistanceUm,
        droppedSamples: chunk.droppedSamples,
      };
      this.buttons.length = 0;
    }

    if (
      chunk.checkpointId !== this.activeId ||
      chunk.chunkIndex !== this.expectedChunk ||
      !this.baseline ||
      chunk.deviceTimestampUs !== this.baseline.deviceTimestampUs ||
      chunk.edgeSequence !== this.baseline.edgeSequence ||
      chunk.maxTravelDistanceUm !== this.baseline.maxTravelDistanceUm ||
      chunk.droppedSamples !== this.baseline.droppedSamples
    ) {
      this.reset();
      throw new Error('PERF_CHECKPOINT id, order, or baseline mismatch');
    }

    this.buttons.push(...chunk.buttons);
    this.expectedChunk += 1;
    if (this.expectedChunk !== PERF_CHECKPOINT_CHUNK_COUNT) {
      return null;
    }

    const complete: PerformanceCheckpoint = {
      ...this.baseline,
      buttons: this.buttons.map((button) => ({ ...button })),
    };
    this.lastCompletedId = this.activeId;
    this.reset();
    return complete;
  }

  reset(clearIdHistory = false): void {
    this.activeId = null;
    this.expectedChunk = 0;
    this.baseline = null;
    this.buttons.length = 0;
    if (clearIdHistory) {
      this.lastCompletedId = null;
    }
  }
}

export class PerformanceTelemetryCache {
  private timestampUs = 0;
  private maxTravelDistanceUm = 0;
  private droppedSamples = 0;
  private readonly buttons: ButtonPerformanceData[] = Array.from(
    { length: PERFORMANCE_BUTTON_COUNT },
    (_, buttonIndex) => ({
      buttonIndex,
      virtualPin: buttonIndex,
      isPressed: false,
      currentDistance: 0,
      pressTriggerDistance: 0,
      pressStartDistance: 0,
      releaseTriggerDistance: 0,
      releaseStartDistance: 0,
    }),
  );

  reset(): void {
    this.timestampUs = 0;
    this.maxTravelDistanceUm = 0;
    this.droppedSamples = 0;
    for (let buttonIndex = 0; buttonIndex < this.buttons.length; buttonIndex += 1) {
      this.buttons[buttonIndex] = {
        buttonIndex,
        virtualPin: buttonIndex,
        isPressed: false,
        currentDistance: 0,
        pressTriggerDistance: 0,
        pressStartDistance: 0,
        releaseTriggerDistance: 0,
        releaseStartDistance: 0,
      };
    }
  }

  applySample(sample: PerformanceSample): void {
    this.timestampUs = sample.deviceTimestampUs;
    this.droppedSamples += sample.droppedSamples;
    for (let index = 0; index < PERFORMANCE_BUTTON_COUNT; index += 1) {
      const button = this.buttons[index];
      button.isPressed = (sample.pressedMask & (1 << index)) !== 0;
      button.currentDistance = micrometresToMillimetres(sample.currentDistanceUm[index]);
    }
  }

  applyEdge(edge: PerformanceEdge): void {
    if (edge.buttonIndex >= this.buttons.length) return;
    this.timestampUs = edge.deviceTimestampUs;
    const button = this.buttons[edge.buttonIndex];
    button.isPressed = edge.pressed;
    button.currentDistance = micrometresToMillimetres(edge.currentDistanceUm);
    button.pressTriggerDistance = micrometresToMillimetres(edge.pressTriggerDistanceUm);
    button.pressStartDistance = micrometresToMillimetres(edge.pressStartDistanceUm);
    button.releaseTriggerDistance = micrometresToMillimetres(edge.releaseTriggerDistanceUm);
    button.releaseStartDistance = micrometresToMillimetres(edge.releaseStartDistanceUm);
  }

  applyCheckpoint(checkpoint: PerformanceCheckpoint): void {
    this.timestampUs = checkpoint.deviceTimestampUs;
    this.maxTravelDistanceUm = checkpoint.maxTravelDistanceUm;
    this.droppedSamples = checkpoint.droppedSamples;
    for (const value of checkpoint.buttons) {
      if (value.buttonIndex < 0 || value.buttonIndex >= this.buttons.length) continue;
      const button = this.buttons[value.buttonIndex];
      button.virtualPin = value.virtualPin;
      button.isPressed = value.pressed;
      button.currentDistance = micrometresToMillimetres(value.currentDistanceUm);
      button.pressTriggerDistance = micrometresToMillimetres(value.pressTriggerDistanceUm);
      button.pressStartDistance = micrometresToMillimetres(value.pressStartDistanceUm);
      button.releaseTriggerDistance = micrometresToMillimetres(value.releaseTriggerDistanceUm);
      button.releaseStartDistance = micrometresToMillimetres(value.releaseStartDistanceUm);
    }
  }

  snapshot(): ButtonPerformanceMonitoringBinaryData {
    return {
      command: BUTTON_PERFORMANCE_MONITORING_CMD,
      isActive: true,
      buttonCount: this.buttons.length,
      timestamp: Math.floor(this.timestampUs / 1000) >>> 0,
      deviceTimestampUs: this.timestampUs,
      maxTravelDistance: micrometresToMillimetres(this.maxTravelDistanceUm),
      droppedSamples: this.droppedSamples,
      buttonData: this.buttons.map((button) => ({ ...button })),
    };
  }
}

/**
 * Applies a point-in-time checkpoint, then replays reliable edges newer than
 * its baseline. This prevents a nine-report checkpoint from rolling the cache
 * backward when newer edge reports arrive before its final chunk.
 */
export function applyCheckpointPreservingEdges(
  cache: PerformanceTelemetryCache,
  checkpoint: PerformanceCheckpoint,
  recentEdges: readonly PerformanceEdge[],
): number | null {
  cache.applyCheckpoint(checkpoint);
  const baseline = checkpoint.edgeSequence ?? 0;
  const replay = recentEdges
    .filter((edge) => sequenceIsAfter(edge.edgeSequence, baseline))
    .sort(
      (left, right) =>
        ((left.edgeSequence - baseline) >>> 0) -
        ((right.edgeSequence - baseline) >>> 0),
    );
  for (const edge of replay) {
    cache.applyEdge(edge);
  }
  return replay.length > 0
    ? replay[replay.length - 1].edgeSequence
    : checkpoint.edgeSequence ?? null;
}

function exactBytes(value: ArrayBuffer | Uint8Array): Uint8Array {
  return value instanceof Uint8Array ? value : new Uint8Array(value);
}

function micrometresToMillimetres(value: number): number {
  return value / 1000;
}

function sequenceIsAfter(value: number, baseline: number): boolean {
  const distance = (value - baseline) >>> 0;
  return distance !== 0 && distance < 0x8000_0000;
}

function nextCheckpointId(value: number): number {
  return value === 0xff ? 1 : value + 1;
}
