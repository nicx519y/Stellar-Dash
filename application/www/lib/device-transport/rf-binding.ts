export const BindingOp = { active: 1, pending: 2, prepare: 3, commit: 4, abort: 5 } as const;
export interface BindingSnapshot {
  version: number; status: number; flags: number; localId: number; peerId: number;
  address: number; generation: number; transaction: number; revision: number; capabilities: number;
}
export interface BindingState { active: BindingSnapshot; pending: BindingSnapshot }
export interface BindingArgs { transaction?: number; expectedRevision?: number; peerId?: number; address?: number; generation?: number }
export type BindingEndpoint = (op: number, args?: BindingArgs) => Promise<BindingSnapshot>;
export const present = (s: BindingSnapshot) => (s.flags & 1) !== 0;
export const modern = (s: BindingSnapshot) => present(s) && (s.flags & 2) !== 0;
export const bindingHex = (n: number) => `0x${n.toString(16).padStart(8, '0').toUpperCase()}`;

export function checkBinding(value: unknown): BindingSnapshot {
  if (!value || typeof value !== 'object') throw new Error('BINDING_UNSUPPORTED');
  const s = value as BindingSnapshot;
  const fields = ['version', 'status', 'flags', 'localId', 'peerId', 'address', 'generation', 'transaction', 'revision', 'capabilities'] as const;
  if (!s || fields.some(k => !Number.isInteger(s[k]) || s[k] < 0 || s[k] > 0xffffffff) ||
      s.version !== 1 || !(s.capabilities & 1)) throw new Error('BINDING_UNSUPPORTED');
  if (s.status) throw new Error(`BINDING_STATUS_${s.status}`);
  if (!s.localId || (s.flags & 8)) throw new Error('BINDING_UNSUPPORTED');
  return s;
}
export async function readBinding(endpoint: BindingEndpoint): Promise<BindingState> {
  for (let attempt = 0; attempt < 3; attempt++) {
    const active = checkBinding(await endpoint(BindingOp.active));
    const pending = checkBinding(await endpoint(BindingOp.pending));
    if (active.localId === pending.localId && active.revision === pending.revision) return { active, pending };
  }
  throw new Error('BINDING_CHANGED');
}
export function matchingBinding(rx: BindingSnapshot, tx: BindingSnapshot) {
  return modern(rx) && modern(tx) && rx.localId === tx.peerId && tx.localId === rx.peerId &&
    rx.address === tx.address && rx.generation === tx.generation && rx.transaction === tx.transaction &&
    rx.address !== 0 && rx.generation !== 0 && rx.transaction !== 0;
}
export function paired(rx: BindingState, tx: BindingState) {
  return !present(rx.pending) && !present(tx.pending) && matchingBinding(rx.active, tx.active);
}

export function canCancelBinding(rx: BindingState, tx: BindingState) {
  if (present(tx.pending) && (!modern(tx.pending) || tx.pending.peerId !== rx.active.localId || matchingBinding(rx.active, tx.pending))) return false;
  if (present(rx.pending) && (!modern(rx.pending) || rx.pending.peerId !== tx.active.localId || matchingBinding(rx.pending, tx.active))) return false;
  return present(rx.pending) || present(tx.pending);
}
export async function cancelReceiverPairing(rxCall: BindingEndpoint, txCall: BindingEndpoint, alive: () => void = () => {}) {
  alive(); let rx = await readBinding(rxCall), tx = await readBinding(txCall);
  if (!canCancelBinding(rx, tx)) throw new Error('BINDING_CONFLICT');
  const rxId = rx.active.localId, txId = tx.active.localId;
  const cancel = async (call: BindingEndpoint, candidate: BindingSnapshot, revision: number) => {
    alive(); let failure: unknown;
    try { checkBinding(await call(BindingOp.abort, { transaction: candidate.transaction, expectedRevision: revision })); }
    catch (e) { failure = e; }
    alive(); const state = await readBinding(call);
    if (state.active.localId !== candidate.localId || present(state.pending) || state.active.transaction === candidate.transaction)
      throw failure ?? new Error('BINDING_INCOMPLETE');
  };
  if (present(rx.pending)) await cancel(rxCall, rx.pending, rx.active.revision);
  alive(); rx = await readBinding(rxCall); tx = await readBinding(txCall);
  if (rx.active.localId !== rxId || tx.active.localId !== txId) throw new Error('BINDING_CHANGED');
  if (present(tx.pending)) {
    if (!canCancelBinding(rx, tx)) throw new Error('BINDING_CONFLICT');
    await cancel(txCall, tx.pending, tx.active.revision);
  }
  alive(); return { rx: await readBinding(rxCall), tx: await readBinding(txCall) };
}

/** USB provisioning only. No connectionMode, reboot, RF start or deferred config. */
export async function pairReceiver(rxCall: BindingEndpoint, txCall: BindingEndpoint,
  transaction: number, alive: () => void = () => {}) {
  const read = async (call: BindingEndpoint) => { alive(); return readBinding(call); };
  let rx = await read(rxCall), tx = await read(txCall);
  const rxId = rx.active.localId, txId = tx.active.localId;
  const refresh = async () => {
    rx = await read(rxCall); tx = await read(txCall);
    if (rx.active.localId !== rxId || tx.active.localId !== txId) throw new Error('BINDING_CHANGED');
  };
  // A transport error is an unknown outcome. Read durable state instead of
  // replaying a mutation or rolling the receiver back to a previous peer.
  const mutate = async (call: BindingEndpoint, op: number, args: BindingArgs,
    verified: () => boolean) => {
    alive(); let failure: unknown;
    try { checkBinding(await call(op, args)); } catch (error) { failure = error; }
    await refresh();
    if (!verified()) throw failure ?? new Error('BINDING_INCOMPLETE');
  };
  if (paired(rx, tx)) return { rx, tx };
  let candidate: BindingSnapshot;
  if (present(rx.pending)) {
    candidate = rx.pending;
    if (!modern(candidate) || candidate.peerId !== txId) throw new Error('BINDING_CONFLICT');
  } else if (present(tx.pending)) {
    // RX commit succeeded but TX commit/reply was interrupted.
    if (!matchingBinding(rx.active, tx.pending)) throw new Error('BINDING_CONFLICT');
    candidate = rx.active;
  } else {
    if (!transaction) throw new Error('BINDING_INVALID');
    await mutate(rxCall, BindingOp.prepare,
      { transaction, expectedRevision: rx.active.revision, peerId: txId },
      () => modern(rx.pending) && rx.pending.transaction === transaction && rx.pending.peerId === txId);
    candidate = rx.pending;
  }
  const txn = candidate.transaction;
  if (present(tx.pending) && !matchingBinding(candidate, tx.pending)) throw new Error('BINDING_CONFLICT');
  if (!present(tx.pending) && !matchingBinding(candidate, tx.active)) {
    await mutate(txCall, BindingOp.prepare, { transaction: txn, expectedRevision: tx.active.revision,
      peerId: rxId, address: candidate.address, generation: candidate.generation },
      () => matchingBinding(candidate, tx.pending));
  }
  if (present(rx.pending)) {
    if (!matchingBinding(rx.pending, tx.pending) && !matchingBinding(rx.pending, tx.active)) throw new Error('BINDING_CHANGED');
    await mutate(rxCall, BindingOp.commit, { transaction: txn, expectedRevision: rx.active.revision },
      () => modern(rx.active) && rx.active.transaction === txn && !present(rx.pending));
  }
  if (present(tx.pending)) {
    if (!matchingBinding(rx.active, tx.pending)) throw new Error('BINDING_CHANGED');
    await mutate(txCall, BindingOp.commit, { transaction: txn, expectedRevision: tx.active.revision },
      () => paired(rx, tx));
  }
  if (!paired(rx, tx)) throw new Error('BINDING_INCOMPLETE');
  return { rx, tx };
}

export function bindingChecksum(bytes: Uint8Array) {
  let h = 2166136261;
  for (const byte of bytes) h = Math.imul(h ^ byte, 16777619) >>> 0;
  return h;
}
export function bindingRequest(op: number, seq: number, args: BindingArgs = {}) {
  const b = new Uint8Array(32), v = new DataView(b.buffer);
  v.setUint32(0, 0x31504252, true); b[4] = 1; b[5] = op; v.setUint16(6, seq, true);
  [args.transaction, args.expectedRevision, args.peerId, args.address, args.generation].forEach((n, i) => {
    if (n !== undefined && (!Number.isInteger(n) || n < 0 || n > 0xffffffff)) throw new Error('BINDING_INVALID');
    v.setUint32(8 + i * 4, n ?? 0, true);
  });
  v.setUint32(28, bindingChecksum(b.subarray(0, 28)), true); return b;
}
export function decodeBinding(bytes: Uint8Array, op: number, seq: number) {
  if (bytes.length !== 48) throw new Error('BINDING_PROTOCOL');
  const v = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  if (v.getUint32(0, true) !== 0x31534252 || bytes[4] !== 1 || bytes[5] !== op ||
      v.getUint16(6, true) !== seq || v.getUint32(44, true) !== bindingChecksum(bytes.subarray(0, 44))) throw new Error('BINDING_PROTOCOL');
  return checkBinding({ version: bytes[4], status: bytes[8], flags: bytes[9], localId: v.getUint32(12, true),
    peerId: v.getUint32(16, true), address: v.getUint32(20, true), generation: v.getUint32(24, true),
    transaction: v.getUint32(28, true), revision: v.getUint32(32, true), capabilities: v.getUint32(36, true) });
}
