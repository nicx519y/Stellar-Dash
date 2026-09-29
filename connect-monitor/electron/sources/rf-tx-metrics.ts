import { txMetricKeys, type RfTxMetrics } from "../../shared/rf-tx-metrics";
type PartialSnapshot = { at: number; span: number; seen: number; first: number; values: number[] };
const pending = new Map<string, PartialSnapshot>();
// Bounded assembly. Missing USB pages never turn into zero-filled statistics.
export function parseTxMetrics(view: DataView, now: number): RfTxMetrics | undefined {
  const page = view.getUint8(6), version = view.getUint8(7);
  if (version !== 1 || page >= 5) return;
  const snapshot = view.getUint16(4, true), at = view.getUint32(8, true), span = view.getUint32(12, true);
  for (const [key, value] of pending) if (now - value.first > 10_000) pending.delete(key);
  const key = `${snapshot}:${at}:${span}`;
  let value = pending.get(key);
  if (!value) {
    if (pending.size >= 4) pending.delete(pending.keys().next().value!);
    value = { at, span, seen: 0, first: now, values: [] }; pending.set(key, value);
  }
  if (value.seen & (1 << page)) return;
  for (let i = 0; i < 4; i++) value.values[page * 4 + i] = view.getUint32(16 + 4 * i, true);
  value.seen |= 1 << page;
  if (value.seen !== 31) return;
  return { version: 1, snapshot, atUs: at, spanUs: span,
    totals: Object.fromEntries(txMetricKeys.map((name, i) => [name, value!.values[i]])) as RfTxMetrics["totals"] };
}
