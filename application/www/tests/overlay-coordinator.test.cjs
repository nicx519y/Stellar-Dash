const test = require('node:test');
const assert = require('node:assert/strict');
const { createOverlayCoordinator, OVERLAY_PRIORITY: P } = require('../lib/overlay-coordinator.ts');

test('disconnect suspends editors, confirmations and draft recovery until connection is ready', () => {
  const overlays = createOverlayCoordinator();
  overlays.request('editor', P.editor);
  overlays.request('confirm', P.confirmation);
  overlays.request('draft', P.recovery);
  overlays.request('connection', P.connection);
  assert.equal(overlays.getSnapshot(), 'connection');
  overlays.request('connection', null);
  assert.equal(overlays.getSnapshot(), 'draft');
  overlays.request('draft', null);
  assert.equal(overlays.getSnapshot(), 'confirm');
  overlays.request('confirm', null);
  assert.equal(overlays.getSnapshot(), 'editor');
});

test('offline and unresolved firmware tasks own the screen; confirmation and completed result yield to disconnect', () => {
  const overlays = createOverlayCoordinator();
  overlays.request('install', P.editor);
  overlays.request('connection', P.connection);
  assert.equal(overlays.getSnapshot(), 'connection');
  overlays.request('install', P.operation);
  assert.equal(overlays.getSnapshot(), 'install');
  // Timeout retains ownership until the user closes the recovery window.
  overlays.request('install', null);
  assert.equal(overlays.getSnapshot(), 'connection');
  overlays.request('install', P.operation);
  overlays.request('install', P.editor);
  assert.equal(overlays.getSnapshot(), 'connection');
  overlays.request('connection', null);
  assert.equal(overlays.getSnapshot(), 'install');
});

test('account dialogs temporarily replace the connection card without losing its request', () => {
  const overlays = createOverlayCoordinator();
  overlays.request('connection', P.connection);
  overlays.request('login', P.account);
  assert.equal(overlays.getSnapshot(), 'login');
  overlays.request('login', null);
  assert.equal(overlays.getSnapshot(), 'connection');
});

test('nested or concurrent prompts show one at a time and unchanged renders do not reorder them', () => {
  const overlays = createOverlayCoordinator();
  overlays.request('first', P.editor);
  overlays.request('second', P.editor);
  overlays.request('first', P.editor);
  assert.equal(overlays.getSnapshot(), 'second');
  overlays.request('confirm', P.confirmation);
  assert.equal(overlays.getSnapshot(), 'confirm');
  overlays.request('confirm', null);
  assert.equal(overlays.getSnapshot(), 'second');
  overlays.request('second', null);
  assert.equal(overlays.getSnapshot(), 'first');
  overlays.request('first', null);
  assert.equal(overlays.getSnapshot(), null);
});

test('unmount releases ownership and subscriptions only report visible owner changes', () => {
  const overlays = createOverlayCoordinator();
  const changes = [];
  const unsubscribe = overlays.subscribe(() => changes.push(overlays.getSnapshot()));
  overlays.request('connection', P.connection);
  overlays.request('editor', P.editor);
  overlays.request('connection', P.connection);
  overlays.request('connection', null);
  unsubscribe();
  overlays.request('editor', null);
  assert.deepEqual(changes, ['connection', 'editor']);
  assert.equal(overlays.getSnapshot(), null);
});
