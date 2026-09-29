import { BindingArgs, BindingSnapshot, bindingRequest, decodeBinding } from './rf-binding';

export interface ReceiverDevice extends EventTarget {
  opened: boolean; vendorId: number; productId: number; productName: string;
  collections: readonly { usagePage?: number; usage?: number }[];
  open(): Promise<void>; close(): Promise<void>;
  sendFeatureReport(id: number, data: BufferSource): Promise<void>;
}
export interface ReceiverHid extends EventTarget {
  getDevices(): Promise<ReceiverDevice[]>;
  requestDevice(options: { filters: { vendorId: number; productId: number; usagePage: number; usage: number }[] }): Promise<ReceiverDevice[]>;
}
export const receiverFilters = [
  { vendorId: 0x045e, productId: 0x02ff, usagePage: 0xff00, usage: 1 },
  { vendorId: 0x1a86, productId: 0xfe0c, usagePage: 0xff00, usage: 1 },
];
export function receiverHid(): ReceiverHid | null {
  if (typeof navigator === 'undefined' || !globalThis.isSecureContext) return null;
  return (navigator as Navigator & { hid?: ReceiverHid }).hid ?? null;
}
export function isReceiver(d: ReceiverDevice) {
  return receiverFilters.some(f => f.vendorId === d.vendorId && f.productId === d.productId) &&
    d.collections.some(c => c.usagePage === 0xff00 && c.usage === 1);
}

export class ReceiverClient {
  private sequence = Math.floor(Math.random() * 65535);
  private closed = false;
  private rejectPending: ((e: Error) => void) | null = null;
  constructor(readonly device: ReceiverDevice) {}
  async open() { await this.device.open(); if (this.closed) { await this.device.close(); throw new Error('BINDING_DISCONNECTED'); } }
  async close() {
    this.closed = true; this.rejectPending?.(new Error('BINDING_DISCONNECTED'));
    if (this.device.opened) await this.device.close();
  }
  request = async (op: number, args?: BindingArgs): Promise<BindingSnapshot> => {
    if (this.closed || !this.device.opened) throw new Error('BINDING_DISCONNECTED');
    if (this.rejectPending) throw new Error('BINDING_BUSY');
    const seq = this.sequence = (this.sequence + 1) & 0xffff;
    return new Promise((resolve, reject) => {
      let mask = 0, settled = false; const bytes = new Uint8Array(48);
      const finish = (error?: Error, value?: BindingSnapshot) => {
        if (settled) return; settled = true;
        clearTimeout(timer); this.device.removeEventListener('inputreport', listener); this.rejectPending = null;
        if (error) reject(error); else resolve(value!);
      };
      const listener = (event: Event) => {
        const { data, reportId } = event as Event & { data: DataView; reportId: number };
        if (reportId !== 0 || data.byteLength !== 32 || data.getUint32(0, true) !== 0x31484252 ||
            data.getUint16(4, true) !== seq || data.getUint8(7) !== 1) return;
        const page = data.getUint8(6); if (page > 1) return;
        bytes.set(new Uint8Array(data.buffer, data.byteOffset + 8, 24), page * 24); mask |= 1 << page;
        if (mask === 3) { try { finish(undefined, decodeBinding(bytes, op, seq)); } catch (e) { finish(e as Error); } }
      };
      const timer = setTimeout(() => finish(new Error('BINDING_TIMEOUT')), 4000);
      this.rejectPending = finish; this.device.addEventListener('inputreport', listener);
      void this.device.sendFeatureReport(0, bindingRequest(op, seq, args)).catch(e => finish(e as Error));
    });
  };
}
