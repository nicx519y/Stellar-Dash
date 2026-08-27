const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const {
  DeviceRequestQueue,
} = require('../lib/device-transport/device-request-queue.ts');
const {
  deviceCommandSchedule,
} = require('../lib/device-transport/device-request-policy.ts');

function deferred() {
  let resolve;
  let reject;
  const promise = new Promise((res, rej) => {
    resolve = res;
    reject = rej;
  });
  return { promise, resolve, reject };
}

test('LED page flushes only on real unmount and gates writes on live readiness', () => {
  const source = fs.readFileSync(
    path.join(__dirname, '../components/leds-setting-content.tsx'),
    'utf8',
  );
  const cleanupStart = source.indexOf('const lifecycle = lifecycleActionsRef.current;');
  const renderStart = source.indexOf('// 渲染hitbox内容', cleanupStart);
  assert.ok(cleanupStart >= 0 && renderStart > cleanupStart);
  const cleanupEffect = source.slice(cleanupStart, renderStart);

  assert.match(source, /deviceConnected && dataIsReady && isInit/);
  assert.match(source, /<Fieldset\.Root disabled={!ledsWriteReady}>/);
  assert.match(cleanupEffect, /lifecycle\.sendPendingCommandImmediately\('update_profile'\)/);
  assert.match(cleanupEffect, /}, \[\]\);/);
  assert.doesNotMatch(source, /}, \[sendPendingCommandImmediately\]\);/);
});

test('the page-exit queue flush callback has stable identity and live client ownership', () => {
  const context = fs.readFileSync(
    path.join(__dirname, '../contexts/gamepad-config-context.tsx'),
    'utf8',
  );
  const callbackStart = context.indexOf('const sendPendingCommandImmediately = useCallback');
  const flushStart = context.indexOf('// 快速清空队列', callbackStart);
  assert.ok(callbackStart >= 0 && flushStart > callbackStart);
  const callback = context.slice(callbackStart, flushStart);

  assert.match(callback, /deviceClientRef\.current/);
  assert.match(callback, /}, \[\]\);/);
});

test('one policy table owns durable debounce, resource keys and timeouts', () => {
  const global = deviceCommandSchedule('update_global_config', {}, false);
  const profile = deviceCommandSchedule(
    'update_profile',
    { profileId: 'profile-2' },
    false,
  );
  const read = deviceCommandSchedule('get_global_config', {}, false);

  assert.equal(global.coalescingKey, 'update_global_config');
  assert.ok(global.debounceMs > 0);
  assert.ok(global.maxWaitMs >= global.debounceMs);
  assert.ok(global.timeoutMs >= 30_000);
  assert.equal(profile.coalescingKey, 'update_profile:profile-2');
  assert.equal(read.coalescingKey, undefined);
  assert.equal(read.debounceMs, 0);
});

test('LED preview and clear share one debounced ephemeral resource key', async () => {
  const previewPolicy = deviceCommandSchedule('push_leds_config', { ledBrightness: 10 }, false);
  const clearPolicy = deviceCommandSchedule('clear_leds_preview', {}, true);
  assert.equal(previewPolicy.coalescingKey, 'led-preview');
  assert.equal(clearPolicy.coalescingKey, 'led-preview');
  assert.ok(previewPolicy.debounceMs > 0);
  assert.equal(clearPolicy.debounceMs, 0);

  const queue = new DeviceRequestQueue(0);
  const calls = [];
  queue.setSendFunction(async (command, params) => {
    calls.push({ command, params });
    return { command };
  });
  const stalePreview = queue.enqueue(
    'push_leds_config',
    { ledBrightness: 10 },
    false,
    previewPolicy,
  );
  const clear = queue.enqueue('clear_leds_preview', {}, true, clearPolicy);

  assert.deepEqual(await stalePreview, { command: 'clear_leds_preview' });
  assert.deepEqual(await clear, { command: 'clear_leds_preview' });
  assert.deepEqual(calls, [{ command: 'clear_leds_preview', params: {} }]);
  queue.destroy();
});

test('latest LED snapshot deletes its old pending preview and moves to the tail', async () => {
  const queue = new DeviceRequestQueue(0);
  const calls = [];
  queue.setSendFunction(async (command, params) => {
    calls.push({ command, params });
    return params;
  });

  const first = queue.enqueue(
    'push_leds_config',
    { ledBrightness: 10 },
    false,
    deviceCommandSchedule('push_leds_config', { ledBrightness: 10 }, false),
  );
  const persistent = queue.enqueue(
    'update_profile',
    { profileId: 'profile-0', profileDetails: { id: 'profile-0', ledsConfigs: { ledBrightness: 20 } } },
    false,
    deviceCommandSchedule('update_profile', { profileId: 'profile-0' }, false),
  );
  const latest = queue.enqueue(
    'push_leds_config',
    { ledBrightness: 90 },
    false,
    deviceCommandSchedule('push_leds_config', { ledBrightness: 90 }, false),
  );

  await queue.flushQueue();
  await Promise.all([first, persistent, latest]);
  assert.deepEqual(calls.map(({ command }) => command), [
    'update_profile',
    'push_leds_config',
  ]);
  assert.deepEqual(calls[1].params, { ledBrightness: 90 });
  queue.destroy();
});

test('coalesced queued writes settle every waiter with the latest payload', async () => {
  const queue = new DeviceRequestQueue(0);
  const calls = [];
  queue.setSendFunction(async (command, params) => {
    calls.push({ command, params });
    return { accepted: params.value };
  });

  const schedule = { coalescingKey: 'update_profile:profile-0' };
  const first = queue.enqueue('update_profile', { value: 1 }, false, schedule);
  const second = queue.enqueue('update_profile', { value: 2 }, false, schedule);
  assert.deepEqual(await first, { accepted: 2 });
  assert.deepEqual(await second, { accepted: 2 });
  assert.deepEqual(calls, [{ command: 'update_profile', params: { value: 2 } }]);
  queue.destroy();
});

test('a newer slider value removes its old pending position and appends at the tail', async () => {
  const queue = new DeviceRequestQueue(50);
  const calls = [];
  queue.setSendFunction(async (command, params) => {
    calls.push({ command, params });
    return { value: params.value };
  });

  const oldSliderValue = queue.enqueue(
    'update_global_config',
    { slider: 'brightness', value: 10 },
    false,
    { coalescingKey: 'slider:brightness' },
  );
  const otherSliderValue = queue.enqueue(
    'update_global_config',
    { slider: 'sleep', value: 30 },
    false,
    { coalescingKey: 'slider:sleep' },
  );
  const latestSliderValue = queue.enqueue(
    'update_global_config',
    { slider: 'brightness', value: 90 },
    false,
    { coalescingKey: 'slider:brightness' },
  );

  assert.deepEqual(queue.getStatus().queuedCommands, [
    'update_global_config',
    'update_global_config',
  ]);
  await queue.flushQueue();
  assert.deepEqual(await oldSliderValue, { value: 90 });
  assert.deepEqual(await latestSliderValue, { value: 90 });
  assert.deepEqual(await otherSliderValue, { value: 30 });
  assert.deepEqual(calls, [
    {
      command: 'update_global_config',
      params: { slider: 'sleep', value: 30 },
    },
    {
      command: 'update_global_config',
      params: { slider: 'brightness', value: 90 },
    },
  ]);
  queue.destroy();
});

test('pending profile writes coalesce only within the same profile', async () => {
  const queue = new DeviceRequestQueue(20);
  const calls = [];
  queue.setSendFunction(async (command, params) => {
    calls.push({ command, params });
    return { profileId: params.profileId, value: params.value };
  });

  const firstP0 = queue.enqueue(
    'update_profile',
    { profileId: 'profile-0', value: 1 },
    false,
    { coalescingKey: 'update_profile:profile-0' },
  );
  const latestP0 = queue.enqueue(
    'update_profile',
    { profileId: 'profile-0', value: 2 },
    false,
    { coalescingKey: 'update_profile:profile-0' },
  );
  const p1 = queue.enqueue(
    'update_profile',
    { profileId: 'profile-1', value: 3 },
    false,
    { coalescingKey: 'update_profile:profile-1' },
  );
  queue.sendPendingCommandImmediately('update_profile');

  assert.deepEqual(await firstP0, { profileId: 'profile-0', value: 2 });
  assert.deepEqual(await latestP0, { profileId: 'profile-0', value: 2 });
  assert.deepEqual(await p1, { profileId: 'profile-1', value: 3 });
  assert.deepEqual(calls, [
    { command: 'update_profile', params: { profileId: 'profile-0', value: 2 } },
    { command: 'update_profile', params: { profileId: 'profile-1', value: 3 } },
  ]);
  queue.destroy();
});

test('coalesced profile patches merge distinct fields instead of dropping either update', async () => {
  const queue = new DeviceRequestQueue(0);
  const calls = [];
  queue.setSendFunction(async (_command, params) => {
    calls.push(params);
    return params;
  });
  const firstParams = {
    profileId: 'profile-0',
    profileDetails: { id: 'profile-0', keysConfig: { mode: 'keys' } },
  };
  const secondParams = {
    profileId: 'profile-0',
    profileDetails: { id: 'profile-0', ledsConfigs: { mode: 'leds' } },
  };
  const policy = deviceCommandSchedule('update_profile', firstParams, false);
  const first = queue.enqueue('update_profile', firstParams, false, policy);
  const second = queue.enqueue(
    'update_profile',
    secondParams,
    false,
    deviceCommandSchedule('update_profile', secondParams, false),
  );
  await queue.flushQueue();
  await Promise.all([first, second]);

  assert.equal(calls.length, 1);
  assert.deepEqual(calls[0].profileDetails, {
    id: 'profile-0',
    keysConfig: { mode: 'keys' },
    ledsConfigs: { mode: 'leds' },
  });
  queue.destroy();
});

test('clear aborts an active operation and propagates its precise cancellation error', async () => {
  const queue = new DeviceRequestQueue(0);
  let observedSignal;
  queue.setSendFunction(async (_command, _params, options) => {
    observedSignal = options.signal;
    return new Promise((_resolve, reject) => {
      options.signal.addEventListener('abort', () => {
        reject(new Error('native session replaced'));
      }, { once: true });
    });
  });

  const request = queue.enqueue('get_global_config', {}, true);
  await new Promise((resolve) => setTimeout(resolve, 5));
  queue.clear(new Error('session replaced'));

  await assert.rejects(request, /native session replaced/);
  assert.equal(observedSignal.aborted, true);
  assert.equal(queue.getStatus().activeCommand, undefined);
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

test('all exclusive operations share one physical lane', async () => {
  const queue = new DeviceRequestQueue(0);
  const firstWrite = deferred();
  let inFlight = 0;
  let maximumInFlight = 0;
  const calls = [];

  const first = queue.runExclusive('first', async () => {
    inFlight += 1;
    maximumInFlight = Math.max(maximumInFlight, inFlight);
    calls.push('first-start');
    await firstWrite.promise;
    calls.push('first-end');
    inFlight -= 1;
  });
  const second = queue.runExclusive('second', async () => {
    inFlight += 1;
    maximumInFlight = Math.max(maximumInFlight, inFlight);
    calls.push('second');
    inFlight -= 1;
  });

  await new Promise((resolve) => setTimeout(resolve, 5));
  assert.deepEqual(calls, ['first-start']);
  firstWrite.resolve();
  await Promise.all([first, second]);
  assert.equal(maximumInFlight, 1);
  assert.deepEqual(calls, ['first-start', 'first-end', 'second']);
  queue.destroy();
});

test('an immediate read flushes an earlier debounced mutation without passing it', async () => {
  const queue = new DeviceRequestQueue(1000);
  const calls = [];
  queue.setSendFunction(async (command) => {
    calls.push(command);
    return { command };
  });

  const write = queue.enqueue('update_global_config', { value: 1 }, false, {
    coalescingKey: 'update_global_config',
    debounceMs: 1000,
    maxWaitMs: 1200,
  });
  const read = queue.runExclusive('get_global_config', async () => {
    calls.push('get_global_config');
  });

  await Promise.all([write, read]);
  assert.deepEqual(calls, ['update_global_config', 'get_global_config']);
  queue.destroy();
});

test('flush rejects when an included device operation fails', async () => {
  const queue = new DeviceRequestQueue(1000);
  queue.setSendFunction(async () => {
    throw new Error('persist failed');
  });
  const write = queue.enqueue('update_global_config', {}, false, {
    coalescingKey: 'update_global_config',
  });
  const flush = queue.flushQueue();
  await assert.rejects(write, /persist failed/);
  await assert.rejects(flush, /persist failed/);
  queue.destroy();
});
