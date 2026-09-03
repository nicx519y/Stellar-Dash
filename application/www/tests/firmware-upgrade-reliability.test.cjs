const test = require('node:test');
const assert = require('node:assert/strict');

const {
  abortFirmwareSessionIfSafe,
  FirmwareFinalizationUncertainError,
  FirmwareUpgradeRestartRequiredError,
  sendFirmwareChunkWithoutAmbiguousRetry,
} = require('../lib/device-transport/firmware-upgrade-reliability.ts');
const {
  DeviceTransportError,
} = require('../lib/device-transport/types.ts');

test('timeout, disconnect and protocol failures never resend an ambiguous firmware chunk', async () => {
  for (const code of ['timeout', 'disconnected', 'protocol']) {
    let attempts = 0;
    await assert.rejects(
      sendFirmwareChunkWithoutAmbiguousRetry(async () => {
        attempts += 1;
        throw new DeviceTransportError(code, `${code} fixture`);
      }),
      (error) => error instanceof FirmwareUpgradeRestartRequiredError &&
        error.failureKind === 'ambiguous' &&
        /Do not resend/.test(error.message),
    );
    assert.equal(attempts, 1, `${code} must not resend the same chunk`);
  }
});

test('a correlated negative firmware ACK stops once and requires a new session', async () => {
  let attempts = 0;
  await assert.rejects(
    sendFirmwareChunkWithoutAmbiguousRetry(async () => {
      attempts += 1;
      return { success: false, chunkIndex: 3, progress: 25, error: 'flash rejected' };
    }),
    (error) => error instanceof FirmwareUpgradeRestartRequiredError &&
      error.failureKind === 'rejected' &&
      /Restart the upgrade session/.test(error.message),
  );
  assert.equal(attempts, 1);
});

test('an ambiguous completion never invokes abort and preserves an uncertainty report', async () => {
  let abortCalls = 0;
  const aborted = await abortFirmwareSessionIfSafe({
    sessionId: 'upgrade-session',
    completionIssued: true,
    completionSettled: false,
    abortSession: async () => { abortCalls += 1; },
  });
  assert.equal(aborted, false);
  assert.equal(abortCalls, 0);
  assert.match(new FirmwareFinalizationUncertainError().message, /Reconnect and verify firmware metadata/);

  const safeAbort = await abortFirmwareSessionIfSafe({
    sessionId: 'upgrade-session',
    completionIssued: false,
    completionSettled: false,
    abortSession: async () => { abortCalls += 1; },
  });
  assert.equal(safeAbort, true);
  assert.equal(abortCalls, 1);
});
