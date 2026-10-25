import { failedLogPage, type FaultLogPage, type FaultLogQuery, type FaultRecord } from '../device-fault-logs';
import { receiverFilters, receiverHid, isReceiver, receiverDeviceLease, type ReceiverDevice } from './rx-receiver-client';

const MAGIC = 0x31474c52;
export function rxLogCrc(bytes: Uint8Array) {
  let crc = 0xffff;
  for (const b of bytes) { crc ^= b << 8; for (let n = 0; n < 8; n++) crc = ((crc << 1) ^ (crc & 0x8000 ? 0x1021 : 0)) & 0xffff; }
  return crc;
}
export function rxLogRequest(op: number, transaction: number, upper = 0, before = 0, pending = 0) {
  const bytes = new Uint8Array(32), v = new DataView(bytes.buffer);
  v.setUint32(0, MAGIC, true); bytes[4] = 1; bytes[5] = op; bytes[6] = pending;
  v.setUint32(8, transaction, true); v.setUint32(12, upper, true); v.setUint32(16, before, true);
  if (op === 4) v.setUint32(20, 0x434c5231, true);
  v.setUint16(30, rxLogCrc(bytes.subarray(0, 30)), true); return bytes;
}
function crc32(bytes: Uint8Array) {
  let c = 0xffffffff;
  for (const b of bytes) { c ^= b; for (let n = 0; n < 8; n++) c = (c >>> 1) ^ (c & 1 ? 0xedb88320 : 0); }
  return (~c) >>> 0;
}
export function decodeRxLog(bytes: Uint8Array): FaultRecord {
  if (bytes.length !== 96) throw new Error('Invalid RX log length');
  const v = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const pending = (bytes[29] & 1) !== 0;
  if (bytes[22] !== 4 || bytes[30] > 14 || crc32(bytes.subarray(0, 88)) !== v.getUint32(88, true) ||
      (!pending && (v.getUint32(0, true) !== 0x314c4658 || v.getUint32(92, true) !== 0xc04d17ed)))
    throw new Error('RX log checksum or format mismatch');
  return { sequence: v.getUint32(4, true), boot: v.getUint32(8, true), uptimeMs: v.getUint32(12, true),
    firmware: v.getUint32(16, true), event: v.getUint16(20, true), severity: bytes[23],
    repeat: v.getUint32(24, true), category: bytes[28], pending,
    data: Array.from({ length: bytes[30] }, (_, i) => v.getUint32(32 + 4 * i, true)) };
}
export class RxLogClient {
  private sequence = (Math.random() * 0xffffffff) >>> 0;
  private active = false;
  private snapshot: FaultLogPage | null = null;
  cancelled: () => boolean = () => false;
  constructor(readonly device: ReceiverDevice, private readonly timeoutMs = 2000) {}
  async request(op: number, upper = 0, before = 0, pending = 0): Promise<{ status: number; bytes: Uint8Array }> {
    if (this.active || !this.device.opened) throw new Error('RX log connection unavailable or busy');
    this.active = true; const transaction = this.sequence = (this.sequence + 1) >>> 0;
    try { return await new Promise((resolve, reject) => {
      const bytes = new Uint8Array(96); let mask = 0, expected = -1, status = -1, settled = false;
      const finish = (error?: Error) => {
        if (settled) return; settled = true; clearTimeout(timer);
        this.device.removeEventListener('inputreport', listener);
        if (error) reject(error); else resolve({ status, bytes: bytes.slice(0, expected) });
      };
      const listener = (event: Event) => {
        const { data: v, reportId } = event as Event & { data: DataView; reportId: number };
        if (reportId || v.byteLength !== 32 || v.getUint32(0, true) !== MAGIC || v.getUint32(8, true) !== transaction) return;
        const b = new Uint8Array(v.buffer, v.byteOffset, 32), page = b[7], length = b[29], pages = b[28];
        if (b[4] !== 1 || b[5] !== op || rxLogCrc(b.subarray(0, 30)) !== v.getUint16(30, true) ||
            length > 96 || pages !== Math.max(1, Math.ceil(length / 16)) || page >= pages || b[6] > 4 ||
            (expected >= 0 && (length !== expected || status !== b[6]))) { finish(new Error('Invalid RX log response')); return; }
        expected = length; status = b[6];
        const part = b.subarray(12, 12 + Math.min(16, Math.max(0, length - page * 16)));
        if (mask & (1 << page)) {
          if (part.some((value, i) => bytes[page * 16 + i] !== value)) finish(new Error('RX log snapshot changed'));
          return;
        }
        bytes.set(part, page * 16); mask |= 1 << page;
        if (mask === (1 << pages) - 1) finish();
      };
      const timer = setTimeout(() => finish(new Error('RX log timeout; firmware may not support logs')), op === 4 ? Math.max(5000, this.timeoutMs) : this.timeoutMs);
      this.device.addEventListener('inputreport', listener);
      void this.device.sendFeatureReport(0, rxLogRequest(op, transaction, upper, before, pending)).catch(e => finish(e instanceof Error ? e : new Error(String(e))));
    }); } finally { this.active = false; }
  }
  fetchPage = async (q: FaultLogQuery): Promise<FaultLogPage> => {
    if (!this.snapshot) {
      const info = await this.request(1);
      if (info.status !== 0 || info.bytes.length !== 36) throw new Error('RX log information unavailable');
      const v = new DataView(info.bytes.buffer);
      if (v.getUint32(0, true) !== 1) throw new Error('Unsupported RX log version');
      this.snapshot = { ...failedLogPage('rx', ''), status: v.getUint32(28, true) ? 'storage_error' : 'ok',
        upper: v.getUint32(4, true), oldest: v.getUint32(8, true), pending: v.getUint32(16, true),
        dropped: v.getUint32(20, true), firmware: v.getUint32(24, true), restricted: !!v.getUint32(32, true), errorMessage: undefined };
      if (this.snapshot.pending > 3) throw new Error('Invalid RX pending count');
    }
    const result = { ...this.snapshot, items: [] as FaultRecord[], more: false, nextBefore: 0 };
    if (q.upper !== undefined && q.upper !== result.upper) throw new Error('RX log snapshot changed');
    try {
    if (q.upper === undefined) for (let i = 1; i <= result.pending; i++) {
      if(this.cancelled()) throw new Error('RX log read cancelled');
      const reply = await this.request(2, result.upper, 0, i);
      if (reply.status !== 0) throw new Error('RX pending log unavailable');
      const r = decodeRxLog(reply.bytes); if (!r.pending) throw new Error('Invalid RX pending log'); result.items.push(r);
    }
    let before = q.before ?? 0;
    if (!result.upper) return result;
    for (let i = 0; i < 8; i++) {
      if(this.cancelled()) throw new Error('RX log read cancelled');
      const reply = await this.request(2, result.upper, before);
      if (reply.status === 3) return result;
      if (reply.status !== 0) throw new Error('RX log storage read failed');
      const r = decodeRxLog(reply.bytes);
      if (r.pending || !r.sequence || r.sequence > result.upper || (before && r.sequence >= before)) throw new Error('RX log cursor did not advance');
      result.items.push(r); before = r.sequence;
      if (before === result.oldest) return result;
    }
    result.more = true; result.nextBefore = before; return result;
    } catch(e) { return { ...result, status: 'unavailable', errorMessage: e instanceof Error ? e.message : String(e) }; }
  };
  async clear() { const reply = await this.request(4); if (reply.status !== 0) throw new Error('RX did not confirm log erase'); }
}

// Called directly from a user gesture. Receiver diagnostics have their own
// transport and lease; never send RX requests through the controller RPC lane.
export async function openReceiverLogs(policyOnly = false) {
  const hid = receiverHid(); if (!hid) throw new Error('WebHID unavailable');
  const devices = await hid.requestDevice({ filters: receiverFilters });
  const device = devices.find(isReceiver); if (!device) throw new Error('No RX selected');
  const lease = receiverDeviceLease();
  await lease.acquire();
  try {
    if (device.opened) throw new Error('RX is already in use');
    await device.open();
  } catch (e) { lease.release(); throw e; }
  const client = new RxLogClient(device);
  return { client, close: async () => {
    try { if (device.opened && !policyOnly) await client.request(3); } finally {
      try { if (device.opened) await device.close(); } finally { lease.release(); }
    }
  } };
}

export async function readReceiverDiagnosticsPolicy(): Promise<import("../diagnostics-policy").DiagnosticsSettings> {
  const session = await openReceiverLogs(true);
  try {
    const result = await session.client.request(5);
    if (result.status !== 0 || result.bytes.length !== 2 || result.bytes[0] !== 2 || result.bytes[1] > 3)
      throw new Error('RX policy unsupported; update receiver firmware');
    return { loggingEnabled: !!(result.bytes[1] & 2), monitoringEnabled: !!(result.bytes[1] & 1) };
  } finally { await session.close(); }
}
