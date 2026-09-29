const test = require('node:test');
const assert = require('node:assert/strict');
const { pairReceiver, paired, readBinding, bindingRequest, decodeBinding, bindingChecksum, canCancelBinding, cancelReceiverPairing } = require('../lib/device-transport/rf-binding.ts');
const { ReceiverClient, isReceiver } = require('../lib/device-transport/rx-receiver-client.ts');
const { elevatedScopesForCommand } = require('../lib/device-transport/scope-policy.ts');

function device(id, rx) {
  const state = { active: null, pending: null, revision: 0, mutations: [], fault: null };
  const snapshot = pending => ({ version: 1, status: 0, flags: (state[pending ? 'pending' : 'active'] ? 3 : 0) | (state.pending ? 4 : 0),
    localId: id, peerId: 0, address: 0, generation: 0, transaction: 0, ...state[pending ? 'pending' : 'active'], revision: state.revision, capabilities: 1 });
  const call = async (op, args = {}) => {
    if (op <= 2) return snapshot(op === 2);
    state.mutations.push(op);
    const fault = state.fault?.op === op ? state.fault : null;
    if (fault) state.fault = null;
    if (fault?.before) throw new Error('power interrupted');
    if (op === 3) {
      assert.equal(args.expectedRevision, state.revision);
      assert.equal(state.pending, null);
      state.revision++;
      state.pending = { transaction: args.transaction, peerId: args.peerId,
        address: rx ? 0x12345600 + state.revision : args.address,
        generation: rx ? state.revision : args.generation };
    } else if (op === 4) {
      assert.equal(args.expectedRevision, state.revision);
      assert.equal(args.transaction, state.pending?.transaction);
      state.active = state.pending; state.pending = null;
    } else if (op === 5) state.pending = null;
    else assert.fail('unexpected operation');
    if (fault) throw new Error('reply lost');
    return snapshot(op === 3);
  };
  return { state, call };
}

test('first bind persists reciprocal identities, refresh confirms; repeating pair does not write', async () => {
  const rx = device(111, true), tx = device(222, false);
  const result = await pairReceiver(rx.call, tx.call, 999);
  assert.ok(paired(result.rx, result.tx));
  assert.deepEqual(rx.state.mutations, [3, 4]); assert.deepEqual(tx.state.mutations, [3, 4]);
  await pairReceiver(rx.call, tx.call, 888);
  assert.deepEqual(rx.state.mutations, [3, 4]);
  assert.ok(paired(await readBinding(rx.call), await readBinding(tx.call)));
});
for (const target of ['rx', 'tx']) for (const op of [3, 4]) {
  test(`lost ${target} ${op} reply is recovered by reading durable state`, async () => {
    const devices = { rx: device(111, true), tx: device(222, false) };
    devices[target].state.fault = { op };
    const result = await pairReceiver(devices.rx.call, devices.tx.call, 77);
    assert.ok(paired(result.rx, result.tx));
    assert.deepEqual(devices[target].state.mutations, [3, 4]);
  });
  test(`interrupted ${target} ${op} resumes without browser transaction cache`, async () => {
    const rx = device(111, true), tx = device(222, false);
    ({ rx, tx })[target].state.fault = { op, before: true };
    await assert.rejects(pairReceiver(rx.call, tx.call, 123));
    assert.ok(!paired(await readBinding(rx.call), await readBinding(tx.call)));
    const result = await pairReceiver(rx.call, tx.call, 456);
    assert.ok(paired(result.rx, result.tx));
    if (target === 'tx' && op === 4) assert.equal(rx.state.mutations.filter(o => o === 4).length, 1);
  });
}
test('changing TX revokes old address and does not change USB/RF mode', async () => {
  const rx = device(111, true), a = device(222, false), b = device(333, false);
  await pairReceiver(rx.call, a.call, 1); const old = rx.state.active.address;
  const result = await pairReceiver(rx.call, b.call, 2);
  assert.notEqual(rx.state.active.address, old);
  assert.ok(paired(result.rx, result.tx));
  assert.ok(!paired(await readBinding(rx.call), await readBinding(a.call)));
  assert.deepEqual(rx.state.mutations, [3, 4, 3, 4]);
});
test('same address alone is not paired; old format and pending are not success', async () => {
  const rx = device(111, true), tx = device(222, false);
  await pairReceiver(rx.call, tx.call, 1);
  let r = await readBinding(rx.call), t = await readBinding(tx.call);
  assert.equal(paired(r, { ...t, active: { ...t.active, peerId: 999 } }), false);
  assert.equal(paired(r, { ...t, active: { ...t.active, flags: 1 } }), false);
  assert.equal(paired(r, { ...t, pending: { ...t.active } }), false);
});
test('unrelated unresolved candidate is rejected without writing either device', async () => {
  const rx = device(111, true), tx = device(222, false);
  await rx.call(3, { transaction: 1, expectedRevision: 0, peerId: 999 });
  await assert.rejects(pairReceiver(rx.call, tx.call, 2), /CONFLICT/);
  assert.deepEqual(rx.state.mutations, [3]); assert.deepEqual(tx.state.mutations, []);
});
test('disconnect during workflow prevents the next mutation', async () => {
  const rx = device(111, true), tx = device(222, false); let calls = 0;
  await assert.rejects(pairReceiver(rx.call, tx.call, 1, () => { if (++calls === 4) throw new Error('gone'); }), /gone/);
  assert.deepEqual(rx.state.mutations, [3]); assert.deepEqual(tx.state.mutations, []);
});
test('binding writes require device.control', () => {
  for (const command of ['prepare_rf_binding', 'commit_rf_binding', 'abort_rf_binding'])
    assert.deepEqual(elevatedScopesForCommand(command), ['device.control']);
});
test('pre-commit cancellation retains old records and resolves both candidates', async () => {
  const rx = device(111, true), tx = device(222, false);
  await pairReceiver(rx.call, tx.call, 1);
  const address = rx.state.active.address;
  rx.state.fault = { op: 4, before: true };
  // Force a new binding by changing TX identity stored at RX.
  rx.state.active.peerId = 999;
  await assert.rejects(pairReceiver(rx.call, tx.call, 2));
  assert.ok(canCancelBinding(await readBinding(rx.call), await readBinding(tx.call)));
  const result = await cancelReceiverPairing(rx.call, tx.call);
  assert.equal(result.rx.active.address, address);
  assert.equal(rx.state.pending, null); assert.equal(tx.state.pending, null);
});
test('RX commit forbids cancellation and old-peer restoration', async () => {
  const rx = device(111, true), tx = device(222, false);
  tx.state.fault = { op: 4, before: true };
  await assert.rejects(pairReceiver(rx.call, tx.call, 1));
  assert.equal(canCancelBinding(await readBinding(rx.call), await readBinding(tx.call)), false);
  await assert.rejects(cancelReceiverPairing(rx.call, tx.call), /CONFLICT/);
  assert.ok(paired(...Object.values(await pairReceiver(rx.call, tx.call, 2))));
});
function response(op, seq) {
  const bytes = new Uint8Array(48), v = new DataView(bytes.buffer);
  v.setUint32(0, 0x31534252, true); bytes[4] = 1; bytes[5] = op; v.setUint16(6, seq, true);
  v.setUint32(12, 111, true); v.setUint32(36, 1, true);
  v.setUint32(44, bindingChecksum(bytes.subarray(0, 44)), true); return bytes;
}
test('wire codec validates checksum, version and request correlation', () => {
  const req = bindingRequest(3, 19, { transaction: 8, peerId: 222 });
  assert.equal(new DataView(req.buffer).getUint32(28, true), bindingChecksum(req.subarray(0, 28)));
  assert.equal(decodeBinding(response(1, 19), 1, 19).localId, 111);
  assert.throws(() => decodeBinding(response(1, 19), 1, 20), /PROTOCOL/);
  const bad = response(1, 19); bad[20] ^= 1; assert.throws(() => decodeBinding(bad, 1, 19), /PROTOCOL/);
  assert.throws(() => bindingRequest(3, 19, { transaction: -1 }), /INVALID/);
});
class FakeHid extends EventTarget {
  opened = false; vendorId = 0x045e; productId = 0x02ff; productName = 'RX';
  collections = [{ usagePage: 0xff00, usage: 1 }];
  async open() { this.opened = true; } async close() { this.opened = false; }
  async sendFeatureReport(id, req) {
    assert.equal(id, 0);
    const seq = new DataView(req.buffer).getUint16(6, true), bytes = response(req[5], seq);
    for (const page of [1, 0]) {
      const report = new Uint8Array(32), view = new DataView(report.buffer);
      view.setUint32(0, 0x31484252, true); view.setUint16(4, seq, true); report[6] = page; report[7] = 1;
      report.set(bytes.subarray(page * 24, page * 24 + 24), 8);
      const event = new Event('inputreport'); event.data = view; event.reportId = 0; this.dispatchEvent(event);
    }
  }
}
test('HID detects only vendor collection and assembles reordered control pages', async () => {
  const hid = new FakeHid(); assert.ok(isReceiver(hid));
  assert.ok(!isReceiver({ ...hid, collections: [{ usagePage: 1, usage: 5 }] }));
  const client = new ReceiverClient(hid); await client.open();
  assert.equal((await client.request(1)).localId, 111);
  await client.close(); await assert.rejects(client.request(1), /DISCONNECTED/);
});
test('disconnect rejects pending HID read immediately', async () => {
  const hid = new FakeHid(); hid.sendFeatureReport = async () => {};
  const client = new ReceiverClient(hid); await client.open();
  const read = client.request(1); const check = assert.rejects(read, /DISCONNECTED/);
  await client.close(); await check;
});
