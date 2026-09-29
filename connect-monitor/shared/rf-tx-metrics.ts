export const txMetricKeys = [
  "due", "attempt", "accepted", "startFailed", "ackSkip", "pauseSkip", "busySkip", "guardSkip",
  "late", "missedEstimate", "ackOk", "ackTimeout", "earlyRelease", "ackUs", "controlSkip",
  "packets5", "packets7", "packets12", "lateMaxUs", "cancelSkip",
] as const;
export type TxMetricKey = typeof txMetricKeys[number];
export interface RfTxMetrics {
  version: 1; snapshot: number; atUs: number; spanUs: number;
  totals: Record<TxMetricKey, number>;
}
export function txMetricsDelta(a: RfTxMetrics, b: RfTxMetrics) {
  const elapsedUs = (b.atUs - a.atUs) >>> 0;
  const sequence = (b.snapshot - a.snapshot) & 65535;
  if (!sequence || sequence > 15 || elapsedUs < 100_000 || elapsedUs > 30_000_000) return null;
  const totals = Object.fromEntries(txMetricKeys.map(k => [k, (b.totals[k] - a.totals[k]) >>> 0])) as Record<TxMetricKey, number>;
  // Reject resets/mixed sessions. LATE_MAX is a lifetime maximum, never delta it.
  const bound = Math.ceil(elapsedUs * 0.02) + 100;
  if (txMetricKeys.some(k => k !== "lateMaxUs" && k !== "ackUs" && totals[k] > bound) ||
      totals.ackUs > elapsedUs + 10_000) return null;
  return { elapsedUs, totals, ackPercent: 100 * totals.ackUs / elapsedUs };
}
