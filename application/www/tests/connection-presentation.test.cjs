const test = require('node:test');
const assert = require('node:assert/strict');
const { connectionErrorMessage, connectionPresentation } = require('../lib/connection-presentation.ts');
const { DeviceConnectionPhase: Phase } = require('../lib/device-transport/device-command-types.ts');

test('connection errors follow the selected language, including permission and fallback errors', () => {
  const error = { transportCode: 'permission-required', type: 'connection', message: '底层中文诊断' };
  assert.match(connectionErrorMessage(error, 'en'), /authorize.*browser chooser/);
  assert.doesNotMatch(connectionErrorMessage(error, 'en'), /[\u4e00-\u9fff]/);
  assert.match(connectionErrorMessage(error, 'zh'), /授权/);
  assert.match(connectionErrorMessage({ type: 'timeout' }, 'en'), /in time/);
  assert.match(connectionErrorMessage({ type: 'protocol' }, 'zh'), /异常响应/);
  assert.equal(connectionErrorMessage(null, 'en'), undefined);
});

test('connection phases ignore previous progress until initialization begins', () => {
  for (const phase of [Phase.DISCOVERING, Phase.OPENING, Phase.ATTESTING, Phase.AUTHORIZING]) {
    const state = connectionPresentation(phase, { completed: 20, total: 20 });
    assert.equal(state.percent, 0);
    assert.equal(state.total, 0);
    assert.equal(state.stage, 0);
  }
});

test('sync displays real counts and switches to preparation after the final read', () => {
  assert.equal(connectionPresentation(Phase.INITIALIZING, { completed: 0, total: 0 }).percent, 0);
  const midway = connectionPresentation(Phase.INITIALIZING, { completed: 7, total: 12 });
  assert.equal(midway.percent, 58);
  assert.equal(midway.stage, 1);
  const complete = connectionPresentation(Phase.INITIALIZING, { completed: 12, total: 12 });
  assert.equal(complete.percent, 100);
  assert.equal(complete.detail, 'finishing');
  assert.equal(complete.stage, 2);
  assert.equal(connectionPresentation(Phase.INITIALIZING, { completed: 13, total: 12 }).percent, 100);
  assert.equal(connectionPresentation(Phase.INITIALIZING, { completed: NaN, total: 0 }).percent, 0);
});
