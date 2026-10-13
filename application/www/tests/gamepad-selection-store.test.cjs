const test = require('node:test');
const assert = require('node:assert/strict');
const { SelectionStore, selectFields } = require('../contexts/gamepad-config/selection-store.ts');

test('calibration progress preserves the firmware snapshot and action identities', () => {
  const firmware = { version: '1.0' };
  const store = new SelectionStore({ firmware, calibration: 0, update: () => 1 });
  const selected = selectFields(store, ['firmware', 'update']);
  const before = selected();
  let notifications = 0;
  store.subscribe(() => notifications++);
  store.publish({ firmware, calibration: 1, update: () => 2 });
  assert.equal(selected(), before);
  assert.equal(selected().update(), 2, 'stable actions call the latest committed implementation');
  assert.equal(notifications, 1);
});

test('action-only replacement refreshes its target without notifying subscribers', () => {
  const store = new SelectionStore({ value: 1, action: () => 'old' });
  const action = store.getSnapshot().action;
  let notifications = 0;
  store.subscribe(() => notifications++);
  store.publish({ value: 1, action: () => 'new' });
  assert.equal(action(), 'new');
  assert.equal(notifications, 0);
});

test('a changed selected value publishes a new snapshot and unsubscribing removes the listener', () => {
  const store = new SelectionStore({ progress: 0, unrelated: 1 });
  const selected = selectFields(store, ['progress']);
  const before = selected();
  let notifications = 0;
  const unsubscribe = store.subscribe(() => notifications++);
  store.publish({ progress: 1, unrelated: 1 });
  assert.notEqual(selected(), before);
  assert.deepEqual(selected(), { progress: 1 });
  unsubscribe();
  store.publish({ progress: 2, unrelated: 1 });
  assert.equal(notifications, 1);
});

test('selectors can switch fields without returning the previous field set', () => {
  const store = new SelectionStore({ connection: false, progress: 0 });
  assert.deepEqual(selectFields(store, ['connection'])(), { connection: false });
  const selected = selectFields(store, ['progress']);
  assert.deepEqual(selected(), { progress: 0 });
  store.publish({ connection: true, progress: 0 });
  const before = selected();
  store.publish({ connection: false, progress: 0 });
  assert.equal(selected(), before);
});

test('rejected asynchronous actions preserve rejection and original argument order', async () => {
  const failure = new Error('configuration session ended');
  const store = new SelectionStore({ action: async (a, b) => { assert.deepEqual([a, b], [3, 4]); throw failure; } });
  await assert.rejects(store.getSnapshot().action(3, 4), error => error === failure);
});
