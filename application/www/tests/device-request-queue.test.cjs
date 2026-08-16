const test = require('node:test');
const assert = require('node:assert/strict');

const {
  DeviceRequestQueue,
} = require('../lib/device-transport/device-request-queue.ts');

function deferred() {
  let resolve;
  let reject;
  const promise = new Promise((res, rej) => {
    resolve = res;
    reject = rej;
  });
  return { promise, resolve, reject };
}

test('coalesced queued writes settle every waiter with the latest payload', async () => {
  const queue = new DeviceRequestQueue(0);
  const calls = [];
  queue.setSendFunction(async (command, params) => {
    calls.push({ command, params });
    return { accepted: params.value };
  });

  const first = queue.enqueue('update_profile', { value: 1 });
  const second = queue.enqueue('update_profile', { value: 2 });
  assert.deepEqual(await first, { accepted: 2 });
  assert.deepEqual(await second, { accepted: 2 });
  assert.deepEqual(calls, [{ command: 'update_profile', params: { value: 2 } }]);
  queue.destroy();
});

test('clear aborts and rejects an active operation without waiting for its native write', async () => {
  const queue = new DeviceRequestQueue(0);
  const nativeWrite = deferred();
  let observedSignal;
  queue.setSendFunction(async (_command, _params, options) => {
    observedSignal = options.signal;
    return nativeWrite.promise;
  });

  const request = queue.enqueue('get_global_config', {}, true);
  await new Promise((resolve) => setTimeout(resolve, 5));
  queue.clear(new Error('session replaced'));

  await assert.rejects(request, /session replaced/);
  assert.equal(observedSignal.aborted, true);
  assert.equal(queue.getStatus().activeCommand, undefined);
  nativeWrite.resolve({ stale: true });
  await new Promise((resolve) => setTimeout(resolve, 0));
  assert.deepEqual(queue.getStatus(), {
    queueSize: 0,
    activeCommand: undefined,
    queuedCommands: [],
  });
  queue.destroy();
});

test('flush waits for both the active operation and queued commands', async () => {
  const queue = new DeviceRequestQueue(0);
  const firstWrite = deferred();
  const calls = [];
  queue.setSendFunction(async (command) => {
    calls.push(command);
    if (command === 'first') return firstWrite.promise;
    return { command };
  });

  const first = queue.enqueue('first', {}, true);
  const second = queue.enqueue('second', {}, true);
  let flushed = false;
  const flush = queue.flushQueue().then(() => { flushed = true; });
  await new Promise((resolve) => setTimeout(resolve, 5));
  assert.equal(flushed, false);
  firstWrite.resolve({ command: 'first' });
  assert.deepEqual(await first, { command: 'first' });
  assert.deepEqual(await second, { command: 'second' });
  await flush;
  assert.deepEqual(calls, ['first', 'second']);
  queue.destroy();
});
