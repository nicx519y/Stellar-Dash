const test = require('node:test');
const assert = require('node:assert/strict');

const {
  PostReadyRequestScheduler,
} = require('../lib/device-transport/post-ready-request-scheduler.ts');
const {
  TelemetryRequestSession,
} = require('../lib/device-transport/telemetry-request-session.ts');

function deferred() {
  let resolve;
  let reject;
  const promise = new Promise((res, rej) => {
    resolve = res;
    reject = rej;
  });
  return { promise, resolve, reject };
}

async function flushMicrotasks(count = 6) {
  for (let index = 0; index < count; index += 1) {
    await Promise.resolve();
  }
}

test('initial clock sync waits for calibration check and monitor setup in exact serial order', async () => {
  const scheduler = new PostReadyRequestScheduler();
  const calibrationResponse = deferred();
  const monitorResponse = deferred();
  const calls = [];
  let activeRequests = 0;
  let maximumActiveRequests = 0;

  scheduler.beginSession();
  const calibrationCheck = scheduler.schedule(async () => {
    activeRequests += 1;
    maximumActiveRequests = Math.max(maximumActiveRequests, activeRequests);
    calls.push('calibration-check');
    await calibrationResponse.promise;
    calls.push('calibration-complete');
    activeRequests -= 1;
    return true;
  });
  const monitorSetup = (async () => {
    const completed = await calibrationCheck;
    assert.equal(completed, true);
    await scheduler.schedule(async () => {
      activeRequests += 1;
      maximumActiveRequests = Math.max(maximumActiveRequests, activeRequests);
      calls.push('monitor-start');
      await monitorResponse.promise;
      calls.push('monitor-complete');
      activeRequests -= 1;
    });
  })();
  scheduler.track(monitorSetup);
  scheduler.releaseInitialBatchWhenIdle(() => {
    calls.push('clock-sync-release');
    void scheduler.schedule(async () => {
      activeRequests += 1;
      maximumActiveRequests = Math.max(maximumActiveRequests, activeRequests);
      calls.push('clock-sync');
      activeRequests -= 1;
    });
  });

  await flushMicrotasks();
  assert.deepEqual(calls, ['calibration-check']);

  calibrationResponse.resolve();
  await flushMicrotasks();
  assert.deepEqual(calls, [
    'calibration-check',
    'calibration-complete',
    'monitor-start',
  ]);

  monitorResponse.resolve();
  await monitorSetup;
  await flushMicrotasks();
  assert.deepEqual(calls, [
    'calibration-check',
    'calibration-complete',
    'monitor-start',
    'monitor-complete',
    'clock-sync-release',
    'clock-sync',
  ]);
  assert.equal(maximumActiveRequests, 1);
});

test('ending a ready session suppresses its stale clock-sync release', async () => {
  const scheduler = new PostReadyRequestScheduler();
  const pending = deferred();
  let releases = 0;

  scheduler.beginSession();
  scheduler.schedule(() => pending.promise).catch(() => undefined);
  scheduler.releaseInitialBatchWhenIdle(() => {
    releases += 1;
  });
  scheduler.endSession();
  pending.resolve();
  await flushMicrotasks();

  assert.equal(releases, 0);
});

test('telemetry session aborts old work and stale checkpoint failure cannot clear reconnect state', () => {
  const session = new TelemetryRequestSession();
  const staleSignal = session.begin();
  const staleCheckpoint = session.beginCheckpoint();
  assert.ok(staleCheckpoint);

  session.end();
  assert.equal(staleSignal.aborted, true);
  const currentSignal = session.begin();
  const currentCheckpoint = session.beginCheckpoint();
  assert.ok(currentCheckpoint);
  assert.equal(currentSignal.aborted, false);

  session.failCheckpoint(staleCheckpoint);
  assert.equal(session.beginCheckpoint(), null);

  session.failCheckpoint(currentCheckpoint);
  assert.ok(session.beginCheckpoint());
  session.end();
  assert.equal(currentSignal.aborted, true);
});
