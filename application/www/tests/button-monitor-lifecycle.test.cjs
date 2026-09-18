const test = require('node:test');
const assert = require('node:assert/strict');

const {
  ButtonMonitorLifecycle,
  SharedButtonMonitorLease,
  shouldStartButtonMonitoring,
} = require('../lib/button-monitor-lifecycle.ts');

function deferred() {
  let resolve;
  let reject;
  const promise = new Promise((res, rej) => {
    resolve = res;
    reject = rej;
  });
  return { promise, resolve, reject };
}

test('button monitoring stays idle until connection data and layout are ready', () => {
  const ready = {
    enabled: true,
    deviceConnected: true,
    dataIsReady: true,
    contextJsReady: true,
    layoutLength: 18,
  };

  assert.equal(shouldStartButtonMonitoring(ready), true);
  assert.equal(shouldStartButtonMonitoring({ ...ready, enabled: false }), false);
  assert.equal(shouldStartButtonMonitoring({ ...ready, deviceConnected: false }), false);
  assert.equal(shouldStartButtonMonitoring({ ...ready, dataIsReady: false }), false);
  assert.equal(shouldStartButtonMonitoring({ ...ready, contextJsReady: false }), false);
  assert.equal(shouldStartButtonMonitoring({ ...ready, layoutLength: 0 }), false);
});

test('failed start never grants ownership and a later cleanup sends no stop', async () => {
  let starts = 0;
  let stops = 0;
  const lifecycle = new ButtonMonitorLifecycle({
    startDevice: async () => {
      starts += 1;
      throw new Error('Manual calibration not completed');
    },
    stopDevice: async () => {
      stops += 1;
    },
  });

  await assert.rejects(lifecycle.start(), /Manual calibration not completed/);
  assert.equal(lifecycle.ownsMonitor, false);
  await lifecycle.stop();
  await lifecycle.dispose();

  assert.equal(starts, 1);
  assert.equal(stops, 0);
});

test('start stop and restart are serialized with exactly one owned stop', async () => {
  const firstStart = deferred();
  const calls = [];
  let activeOperations = 0;
  let maximumConcurrentOperations = 0;
  let startCount = 0;

  const lifecycle = new ButtonMonitorLifecycle({
    startDevice: async () => {
      activeOperations += 1;
      maximumConcurrentOperations = Math.max(maximumConcurrentOperations, activeOperations);
      startCount += 1;
      calls.push(`start:${startCount}`);
      if (startCount === 1) await firstStart.promise;
      activeOperations -= 1;
    },
    stopDevice: async () => {
      activeOperations += 1;
      maximumConcurrentOperations = Math.max(maximumConcurrentOperations, activeOperations);
      calls.push('stop');
      activeOperations -= 1;
    },
    onAcquired: () => calls.push('acquired'),
    onReleased: () => calls.push('released'),
  });

  const start = lifecycle.start();
  const stop = lifecycle.stop();
  const restart = lifecycle.start();
  await new Promise((resolve) => setTimeout(resolve, 0));
  assert.deepEqual(calls, ['start:1']);

  firstStart.resolve();
  await Promise.all([start, stop, restart]);

  assert.deepEqual(calls, [
    'start:1',
    'acquired',
    'released',
    'stop',
    'start:2',
    'acquired',
  ]);
  assert.equal(maximumConcurrentOperations, 1);
  assert.equal(lifecycle.ownsMonitor, true);

  await lifecycle.dispose();
  assert.equal(lifecycle.ownsMonitor, false);
  assert.equal(calls.filter((call) => call === 'stop').length, 2);
});

test('dispose during an in-flight start releases the device without activating local listeners', async () => {
  const pendingStart = deferred();
  const startEntered = deferred();
  let stops = 0;
  let acquired = 0;
  let released = 0;
  const lifecycle = new ButtonMonitorLifecycle({
    startDevice: async () => {
      startEntered.resolve();
      return pendingStart.promise;
    },
    stopDevice: async () => {
      stops += 1;
    },
    onAcquired: () => {
      acquired += 1;
    },
    onReleased: () => {
      released += 1;
    },
  });

  const start = lifecycle.start();
  await startEntered.promise;
  const dispose = lifecycle.dispose();
  pendingStart.resolve();
  await Promise.all([start, dispose]);

  assert.equal(acquired, 0);
  assert.equal(released, 0);
  assert.equal(stops, 1);
  assert.equal(lifecycle.ownsMonitor, false);
});

test('StrictMode effect replay can reactivate after cleanup without reviving stale start work', async () => {
  const firstStart = deferred();
  const firstStartEntered = deferred();
  let starts = 0;
  let stops = 0;
  let acquired = 0;
  const lifecycle = new ButtonMonitorLifecycle({
    startDevice: async () => {
      starts += 1;
      if (starts === 1) {
        firstStartEntered.resolve();
        await firstStart.promise;
      }
    },
    stopDevice: async () => {
      stops += 1;
    },
    onAcquired: () => {
      acquired += 1;
    },
  });

  const staleStart = lifecycle.start();
  await firstStartEntered.promise;
  const cleanup = lifecycle.dispose();
  lifecycle.activate();
  const currentStart = lifecycle.start();
  firstStart.resolve();
  await Promise.all([staleStart, cleanup, currentStart]);

  assert.equal(starts, 2);
  assert.equal(stops, 1);
  assert.equal(acquired, 1);
  assert.equal(lifecycle.ownsMonitor, true);
  await lifecycle.dispose();
});

test('two hook owners share one device start and only the final release stops it', async () => {
  const shared = new SharedButtonMonitorLease();
  const startGate = deferred();
  let starts = 0;
  let stops = 0;
  shared.beginSession();

  const startDevice = async () => {
    starts += 1;
    await startGate.promise;
  };
  const firstAcquire = shared.acquire(startDevice);
  const secondAcquire = shared.acquire(startDevice);
  await Promise.resolve();
  assert.equal(starts, 1);
  assert.equal(shared.ownerCount, 0);

  startGate.resolve();
  const [first, second] = await Promise.all([firstAcquire, secondAcquire]);
  assert.equal(shared.ownerCount, 2);

  await shared.release(first, async () => {
    stops += 1;
  });
  assert.equal(stops, 0);
  assert.equal(shared.ownerCount, 1);

  await shared.release(second, async () => {
    stops += 1;
  });
  assert.equal(stops, 1);
  assert.equal(shared.ownerCount, 0);
});

test('a failed shared start grants no lease and is coalesced for concurrent hooks', async () => {
  const shared = new SharedButtonMonitorLease();
  let starts = 0;
  shared.beginSession();
  const startDevice = async () => {
    starts += 1;
    throw new Error('calibration incomplete');
  };

  const first = shared.acquire(startDevice);
  const second = shared.acquire(startDevice);
  await Promise.all([
    assert.rejects(first, /calibration incomplete/),
    assert.rejects(second, /calibration incomplete/),
  ]);

  assert.equal(starts, 1);
  assert.equal(shared.ownerCount, 0);
});

test('a disconnected session clears ownership and its delayed cleanup cannot stop a reconnect', async () => {
  const shared = new SharedButtonMonitorLease();
  let starts = 0;
  let stops = 0;

  shared.beginSession();
  const staleToken = await shared.acquire(async () => {
    starts += 1;
  });
  shared.endSession();

  shared.beginSession();
  const currentToken = await shared.acquire(async () => {
    starts += 1;
  });
  await shared.release(staleToken, async () => {
    stops += 1;
  });

  assert.equal(starts, 2);
  assert.equal(stops, 0);
  assert.equal(shared.ownerCount, 1);

  await shared.release(currentToken, async () => {
    stops += 1;
  });
  assert.equal(stops, 1);
  assert.equal(shared.ownerCount, 0);
});

test('a suspended shared monitor can resume after a failed configuration commit', async () => {
  const shared = new SharedButtonMonitorLease();
  const calls = [];
  shared.beginSession();
  const token = await shared.acquire(async () => calls.push('start'));

  await shared.suspend(async () => calls.push('stop'));
  assert.equal(shared.ownerCount, 1);
  await shared.resume(async () => calls.push('restart'));
  await shared.release(token, async () => calls.push('release-stop'));

  assert.deepEqual(calls, ['start', 'stop', 'restart', 'release-stop']);
});

test('finalizing a suspended monitor invalidates old page cleanup', async () => {
  const shared = new SharedButtonMonitorLease();
  let stops = 0;
  shared.beginSession();
  const stale = await shared.acquire(async () => undefined);
  await shared.suspend(async () => { stops += 1; });
  shared.finalizeSuspension();

  await shared.release(stale, async () => { stops += 1; });
  const current = await shared.acquire(async () => undefined);
  await shared.release(current, async () => { stops += 1; });

  assert.equal(stops, 2);
});

test('a destination page waits for a background save and acquires monitoring after resume', async () => {
  const shared = new SharedButtonMonitorLease();
  const calls = [];
  shared.beginSession();
  const source = await shared.acquire(async () => calls.push('start-source'));
  await shared.suspend(async () => calls.push('stop-source'));

  let destinationSettled = false;
  const destinationPromise = shared.acquire(async () => calls.push('start-destination'))
    .then((token) => {
      destinationSettled = true;
      return token;
    });
  await Promise.resolve();
  assert.equal(destinationSettled, false);

  await shared.release(source, async () => calls.push('stale-stop'));
  await shared.resume(async () => calls.push('resume-destination'));
  const destination = await destinationPromise;
  assert.equal(destinationSettled, true);
  await shared.release(destination, async () => calls.push('stop-destination'));

  assert.deepEqual(calls, [
    'start-source',
    'stop-source',
    'start-destination',
    'stop-destination',
  ]);
});
