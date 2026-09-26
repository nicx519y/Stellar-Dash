import { BindingArgs, BindingSnapshot, bindingChecksum } from './rf-binding';
import type { ReceiverDevice, ReceiverHid } from './rx-receiver-client';

export interface MockBindingState { revision: number; active: BindingArgs | null; pending: BindingArgs | null }
export class MockBindingStore {
  state: MockBindingState = { revision: 0, active: null, pending: null };
  constructor(readonly id: number, readonly receiver = false) {}
  request(op: number, args: BindingArgs = {}): BindingSnapshot {
    let status = 0; const s = this.state;
    if (op >= 3) {
      if (!args.transaction) status = 1;
      else if (op === 4 && s.active?.transaction === args.transaction) { /* idempotent */ }
      else if (op === 3 && s.pending?.transaction === args.transaction && s.pending.peerId === args.peerId) { /* idempotent */ }
      else if (args.expectedRevision !== s.revision) status = 2;
      else if (op === 3) {
        if (s.pending) status = 2;
        else if (!args.peerId || (!this.receiver && (!args.address || !args.generation))) status = 1;
        else {
          ++s.revision; s.pending = { transaction: args.transaction, peerId: args.peerId,
            address: this.receiver ? 0x735acd00 + s.revision : args.address,
            generation: this.receiver ? s.revision : args.generation };
        }
      } else if (s.active?.transaction === args.transaction || s.pending?.transaction !== args.transaction) status = 2;
      else if (op === 4) { s.active = s.pending; s.pending = null; }
      else if (op === 5) s.pending = null;
      else status = 1;
    }
    const b = op === 2 || op === 3 ? s.pending : s.active;
    return { version: 1, status, flags: (b ? 3 : 0) | (s.pending ? 4 : 0), localId: this.id,
      peerId: b?.peerId ?? 0, address: b?.address ?? 0, generation: b?.generation ?? 0,
      transaction: b?.transaction ?? 0, revision: s.revision, capabilities: 1 };
  }
}

/** Offline preview never discovers or writes a physical receiver. */
class MockReceiver extends EventTarget implements ReceiverDevice {
  opened = false; vendorId = 0x045e; productId = 0x02ff; productName = 'Mock RX Receiver';
  collections = [{ usagePage: 0xff00, usage: 1 }];
  store = new MockBindingStore(0x52580001, true);
  constructor() {
    super(); try { const data = sessionStorage.getItem('hbox.mock.rx-binding'); if (data) this.store.state = JSON.parse(data); } catch { /* optional preview persistence */ }
  }
  async open() { this.opened = true; }
  async close() { this.opened = false; }
  async sendFeatureReport(_id: number, data: BufferSource) {
    const input = data instanceof ArrayBuffer ? new Uint8Array(data) : new Uint8Array(data.buffer, data.byteOffset, data.byteLength);
    const req = new DataView(input.buffer, input.byteOffset, input.byteLength);
    const s = this.store.request(input[5], { transaction: req.getUint32(8, true), expectedRevision: req.getUint32(12, true),
      peerId: req.getUint32(16, true), address: req.getUint32(20, true), generation: req.getUint32(24, true) });
    try { sessionStorage.setItem('hbox.mock.rx-binding', JSON.stringify(this.store.state)); } catch { /* optional */ }
    const bytes = new Uint8Array(48), v = new DataView(bytes.buffer);
    v.setUint32(0, 0x31534252, true); bytes.set(input.subarray(4, 8), 4); bytes[8] = s.status; bytes[9] = s.flags;
    [s.localId, s.peerId, s.address, s.generation, s.transaction, s.revision, s.capabilities].forEach((n, i) => v.setUint32(12 + i * 4, n, true));
    v.setUint32(44, bindingChecksum(bytes.subarray(0, 44)), true);
    for (let page = 0; page < 2; page++) {
      const report = new Uint8Array(32), rv = new DataView(report.buffer);
      rv.setUint32(0, 0x31484252, true); report[4] = input[6]; report[5] = input[7]; report[6] = page; report[7] = 1;
      report.set(bytes.subarray(page * 24, page * 24 + 24), 8);
      const event = new Event('inputreport'); Object.assign(event, { data: rv, reportId: 0 }); this.dispatchEvent(event);
    }
  }
}
class MockHid extends EventTarget implements ReceiverHid {
  device = new MockReceiver();
  async getDevices() { return sessionStorage.getItem('hbox.mock.rx-authorized') ? [this.device] : []; }
  async requestDevice() { sessionStorage.setItem('hbox.mock.rx-authorized', '1'); return [this.device]; }
}
let hid: ReceiverHid | null = null;
export function getReceiverHid(): ReceiverHid | null {
  if (typeof window === 'undefined') return null;
  return hid ??= new MockHid();
}
