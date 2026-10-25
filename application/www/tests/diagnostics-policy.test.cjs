const test = require('node:test');
const assert = require('node:assert/strict');
const { parseDiagnosticsPolicy } = require('../lib/diagnostics-policy.ts');
test('rejects legacy, missing and unknown policy states', () => {
  for (const value of [null, {}, {version:1,enabled:false},
    {version:2,loggingEnabled:false},
    {version:2,loggingEnabled:false,monitoringEnabled:0,controller:'applied',tx:'applied',rx:'pending'},
    {version:2,loggingEnabled:false,monitoringEnabled:false,controller:'applied',tx:'timeout',rx:'pending'}])
    assert.throws(() => parseDiagnosticsPolicy(value));
});
test('all four independent settings round-trip', () => {
  for (const loggingEnabled of [false,true]) for (const monitoringEnabled of [false,true]) {
    const p=parseDiagnosticsPolicy({version:2,loggingEnabled,monitoringEnabled,controller:'applied',tx:'unsupported',rx:'pending'});
    assert.equal(p.loggingEnabled,loggingEnabled);assert.equal(p.monitoringEnabled,monitoringEnabled);
    assert.equal(p.tx,'unsupported');assert.equal(p.rx,'pending');
  }
});
