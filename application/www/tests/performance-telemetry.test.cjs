const test = require('node:test');
const assert = require('node:assert/strict');
const { PerformanceTelemetryCache } = require('../lib/device-transport/performance-codec.ts');
const { PerformanceTelemetryController } = require('../lib/device-transport/performance-telemetry.ts');

const sample = (time, pressed = false, distance = 200) => ({
  deviceTimestampUs: time, pressedMask: pressed ? 1 : 0, droppedSamples: 0,
  currentDistanceUm: Array(18).fill(distance),
});
const edge = (time, pressed = true) => ({
  deviceTimestampUs: time, edgeSequence: 1, buttonIndex: 0, pressed,
  rawAdc: 1000, currentDistanceUm: 1500, pressTriggerDistanceUm: 1400,
  pressStartDistanceUm: 1600, releaseTriggerDistanceUm: 300, releaseStartDistanceUm: 200,
});

test('late checkpoint and older sample never roll back a newer press or its travel', () => {
  const cache = new PerformanceTelemetryCache();
  cache.applyEdge(edge(300));
  cache.applySample(sample(400, true, 1700));
  cache.applyCheckpoint({
    deviceTimestampUs: 200, edgeSequence: 0, maxTravelDistanceUm: 4000, droppedSamples: 0,
    buttons: [{ ...edge(200, false), virtualPin: 8, pressTriggerDistanceUm: 0 }],
  });
  cache.applySample(sample(100));
  const state = cache.snapshot();
  assert.equal(state.deviceTimestampUs, 400);
  assert.equal(state.buttonData[0].isPressed, true);
  assert.equal(state.buttonData[0].currentDistance, 1.7);
  assert.equal(state.buttonData[0].pressTriggerDistance, 1.4);
  assert.equal(state.buttonData[0].virtualPin, 8);
  assert.equal(state.maxTravelDistance, 4);
});

test('timestamps handle wraparound and reset, independently for each button', () => {
  const cache = new PerformanceTelemetryCache();
  cache.applySample(sample(0xfffffff0));
  cache.applyEdge(edge(20));
  cache.applySample(sample(10));
  assert.equal(cache.snapshot().buttonData[0].isPressed, true);
  assert.equal(cache.snapshot().buttonData[1].currentDistance, 0.2);
  cache.applySample(sample(30));
  assert.equal(cache.snapshot().buttonData[0].isPressed, false);
  cache.reset(); cache.applySample(sample(1, true));
  assert.equal(cache.snapshot().deviceTimestampUs, 1);
  assert.equal(cache.snapshot().buttonData[0].isPressed, true);
});

test('controller processes authenticated events immediately in order, renders once, and cancels stale renders', () => {
  const previous = global.requestAnimationFrame;
  const frames = []; const listeners = new Map(); const snapshots = [];
  global.requestAnimationFrame = callback => { frames.push(callback); return frames.length; };
  const controller = new PerformanceTelemetryController({
    kind: 'mock', subscribe: (name, callback) => { listeners.set(name, callback); return () => listeners.delete(name); },
    request: async () => ({}),
  });
  try {
    controller.start({ deferClockSync: true });
    controller.subscribe(value => snapshots.push(value));
    const bytes = Buffer.alloc(44); bytes.writeUInt32LE(100); bytes[4] = 1;
    listeners.get('performance.sample')({ data: bytes });
    bytes.writeUInt32LE(200); bytes[4] = 0;
    listeners.get('performance.sample')({ data: bytes });
    assert.equal(frames.length, 1);
    frames.shift()();
    assert.equal(snapshots.length, 1);
    assert.equal(snapshots[0].deviceTimestampUs, 200);
    assert.equal(snapshots[0].buttonData[0].isPressed, false);
    listeners.get('performance.sample')({ data: bytes });
    controller.stop(); frames.shift()();
    assert.equal(snapshots.length, 1);
  } finally {
    controller.stop();
    if (previous === undefined) delete global.requestAnimationFrame; else global.requestAnimationFrame = previous;
  }
});
