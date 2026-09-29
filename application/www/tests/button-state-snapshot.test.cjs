const test = require('node:test');
const assert = require('node:assert/strict');

const {
  reconcileButtonStateSnapshot,
} = require('../lib/button-state-snapshot.ts');

test('button snapshot preserves a press/release edge across render-equivalent calls', () => {
  const pressed = reconcileButtonStateSnapshot(0, 1 << 3, 22, [3]);

  assert.equal(pressed.mask, 1 << 3);
  assert.equal(pressed.buttonStates[3], 1);
  assert.deepEqual(pressed.transitions, [{ buttonId: 3, pressed: true }]);

  // A React render occurs between these calls. The caller retains pressed.mask
  // in a ref and therefore still detects the subsequent triggerMask=0 release.
  const released = reconcileButtonStateSnapshot(pressed.mask, 0, 22, [3]);

  assert.equal(released.mask, 0);
  assert.equal(released.buttonStates[3], -1);
  assert.deepEqual(released.transitions, [{ buttonId: 3, pressed: false }]);
});

test('button snapshot handles partial and final releases in a chord', () => {
  const chordMask = (1 << 1) | (1 << 4);
  const chord = reconcileButtonStateSnapshot(0, chordMask, 22, [1, 4]);
  assert.deepEqual(chord.transitions, [
    { buttonId: 1, pressed: true },
    { buttonId: 4, pressed: true },
  ]);

  const partialRelease = reconcileButtonStateSnapshot(
    chord.mask,
    1 << 4,
    22,
    [1, 4],
  );
  assert.equal(partialRelease.buttonStates[1], -1);
  assert.equal(partialRelease.buttonStates[4], 1);
  assert.deepEqual(partialRelease.transitions, [
    { buttonId: 1, pressed: false },
  ]);

  const finalRelease = reconcileButtonStateSnapshot(
    partialRelease.mask,
    0,
    22,
    [1, 4],
  );
  assert.deepEqual(finalRelease.transitions, [
    { buttonId: 4, pressed: false },
  ]);
});

test('button snapshot ignores disabled, duplicate, and out-of-range ids', () => {
  const result = reconcileButtonStateSnapshot(
    0,
    (1 << 2) | (1 << 5),
    6,
    [2, 2, 5, -1, 32],
    [5],
  );

  assert.equal(result.mask, 1 << 2);
  assert.equal(result.buttonStates[2], 1);
  assert.equal(result.buttonStates[5], -1);
  assert.deepEqual(result.transitions, [{ buttonId: 2, pressed: true }]);
});

test('an unchanged full snapshot does not emit duplicate transitions', () => {
  const mask = (1 << 0) | (1 << 17);
  const result = reconcileButtonStateSnapshot(mask, mask, 22, [0, 17]);

  assert.equal(result.buttonStates[0], 1);
  assert.equal(result.buttonStates[17], 1);
  assert.deepEqual(result.transitions, []);
});
