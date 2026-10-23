import type { FaultRecord } from './device-fault-logs';
import { chargeEvents, decodeChargeRecord } from './charge-test-logs';

export const batteryEvents = [
  ['BAT_TEST_START', '测试开始', 'Test started'],
  ['BAT_DISCHARGE_START', '开始放电', 'Discharge started'],
  ['BAT_SAMPLE_SUMMARY', '电量趋势摘要', 'Battery trend'],
  ['BAT_CONFIG_CHANGED', '测试配置变化', 'Configuration changed'],
  ['BAT_DATA_QUALITY_CHANGED', '采样质量变化', 'Sample quality changed'],
  ['BAT_THRESHOLD_CHANGED', '阈值确认变化', 'Threshold confirmation changed'],
  ['BAT_LEVEL_CHANGED', '电量状态变化', 'Battery level changed'],
  ['BAT_NOTICE_CHANGED', '屏幕提示变化', 'Screen notice changed'],
  ['BAT_PROTECTION_ENTER', '进入电池保护', 'Battery protection entered'],
  ['BAT_SLEEP_SUMMARY', '休眠统计', 'Sleep summary'],
  ['BAT_INPUT_POWER_CHANGED', '外部供电变化', 'Input power changed'],
  ['BAT_CHARGE_CHANGED', '充电状态变化', 'Charging changed'],
  ['BAT_RECOVERY_ACTION', '恢复操作', 'Recovery action'],
  ['BAT_STORAGE_STATUS', '记录存储状态', 'Storage status'],
  ['BAT_TEST_END', '测试结束', 'Test ended'],
] as const;

export function batteryEventName(event: number, language: 'zh' | 'en') {
  if (event >= 120 && event <= 127) return chargeEvents[event - 120]?.[language === 'zh' ? 1 : 2];
  return batteryEvents[event - 100]?.[language === 'zh' ? 1 : 2];
}
export function decodeBatteryRecord(r: FaultRecord): Record<string, number | string | boolean | null> {
  if (r.event >= 120) return decodeChargeRecord(r);
  const d = r.data;
  if (r.event < 100 || r.event > 114 || (d[0] !== 1 && d[0] !== 2) || d.length < 2) return { unsupported: true };
  const base = { type: batteryEvents[r.event - 100][0], schemaVersion: d[0], session: d[1] };
  if (r.event === 102 && d.length === 14) return { ...base,
    fromUptimeMs: d[2], toUptimeMs: d[3], minMv: d[8] ? d[4] : null, maxMv: d[8] ? d[5] : null,
    firstSocPercent: d[8] ? d[6] / 10 : null, lastSocPercent: d[8] ? d[7] / 10 : null,
    validSamples: d[8], missingSamples: d[9], dischargeMs: d[10],
    ordinarySleepMs: d[11], storagePauseMs: d[12], dropped: d[13] };
  if (r.event === 114 && d.length >= 9) return { ...base, reason: d[2], elapsedMs: d[2] === 4 ? null : d[3],
    dischargeMs: d[2] === 4 ? null : d[4], ordinarySleepMs: d[2] === 4 ? null : d[5], storagePauseMs: d[2] === 4 ? null : d[6],
    dropped: d[7], incomplete: d[8] !== 0 };
  if ((r.event === 100 || r.event === 103) && d.length === 14) return { ...base,
    connectionMode: d[2], inputMode: d[3], wirelessRateHz: d[4], screenBrightness: d[5],
    screenTimeoutSeconds: d[6], screenStandbyEnabled: d[7] !== 0, autoSleepEnabled: d[8] !== 0,
    autoSleepMs: d[9], keyLedEnabled: !!(d[10] & 1), keyBrightness: (d[10] >>> 8) & 255,
    keyEffect: d[10] >>> 16, ambientLedEnabled: !!(d[11] & 1), ambientBrightness: (d[11] >>> 8) & 255,
    ambientEffect: d[11] >>> 16, ledConfigCrc: d[12], profileIdCrc: d[13] };
  if (r.event === 109 && d.length === 14) return { ...base,
    ordinarySleepMs: d[2], stopAttempts: d[3], stopFailures: d[4], stopWaitMs: d[5], storagePauseMs: d[6], stopGateMask: d[7],
    ordinaryStops: d[8], ordinaryStopMs: d[9], protectionStops: d[10], protectionStopMs: d[11],
    ordinaryStopFailures: d[12], protectionStopFailures: d[13] };
  if (d.length >= 10) return { ...base, detail: d[2], extra: d[3],
    cellMv: d[6] ? d[4] : null, socPercent: d[6] ? d[5] / 10 : null, valid: d[6] !== 0, batteryLevel: d[7],
    faultBits: d[8], sampledAtMs: d[9] };
  return { ...base, unsupported: true };
}
export type BatteryCycleIssue = 'partial_log' | 'missing_records' | 'pending_records' |
  'configuration_changed' | 'sample_gap' | 'invalid_sample' | 'reboot' | 'unknown_start';
export type BatteryCycleEnd = 'protection' | 'external_power' | 'charging' | 'manual' |
  'timeout' | 'reboot' | 'storage_failure' | 'unknown' | 'ongoing';
export interface BatteryCycle {
  id: number;
  start: FaultRecord;
  end?: FaultRecord;
  endReason: BatteryCycleEnd;
  status: 'valid' | 'interrupted' | 'unverified' | 'recording';
  issues: BatteryCycleIssue[];
  elapsedMs: number | null;
  enduranceMs: number | null;
  startSocPercent: number | null;
  endSocPercent: number | null;
  milestones: { toLowMs: number | null; toProtectionMs: number | null; afterWarningMs: number | null };
}
type BatteryLogCoverage = { more?: boolean; status?: string; overwritten?: boolean; dropped?: number };
const duration = (a?: FaultRecord, b?: FaultRecord) => a && b && a.boot === b.boot ?
  (b.uptimeMs - a.uptimeMs) >>> 0 : null;
const snapshotSoc = (r: FaultRecord) => r.data.length === 10 && r.data[6] ? r.data[5] / 10 : null;
const missingSequences = (records: FaultRecord[]) => {
  const persisted = records.filter(r => !r.pending);
  return persisted.some((r, i) => i > 0 && r.sequence !== persisted[i - 1].sequence + 1);
};

/** Reconstruct discharge boundaries from v1 power events, including older
 * firmware which emits DISCHARGE_START only once per test session. */
function dischargeCycles(ordered: FaultRecord[], coverage: BatteryLogCoverage): BatteryCycle[] {
  const cycles: BatteryCycle[] = [];
  let active: BatteryCycle | undefined;
  let low: FaultRecord | undefined;
  let qualityBad = false;
  let inputPowered: boolean | undefined;
  let protectedBattery = false;
  let last: FaultRecord | undefined;
  const issue = (cycle: BatteryCycle, reason: BatteryCycleIssue) => {
    if (!cycle.issues.includes(reason)) cycle.issues.push(reason);
  };
  const close = (r: FaultRecord, reason: BatteryCycleEnd) => {
    if (!active) return;
    active.end = r; active.endReason = reason;
    active.elapsedMs = reason === 'reboot' ? null : duration(active.start, r);
    active.endSocPercent = snapshotSoc(r);
    if (reason === 'protection') {
      active.milestones.toProtectionMs = active.elapsedMs;
      active.milestones.afterWarningMs = duration(low, r);
    }
    active = undefined; low = undefined;
  };
  const begin = (r: FaultRecord) => {
    active = { id: cycles.length + 1, start: r, endReason: 'ongoing', status: 'recording', issues: [],
      elapsedMs: 0, enduranceMs: null, startSocPercent: snapshotSoc(r), endSocPercent: null,
      milestones: { toLowMs: null, toProtectionMs: null, afterWarningMs: null } };
    if (qualityBad || !r.data[6]) issue(active, 'invalid_sample');
    cycles.push(active); low = undefined;
  };
  for (const r of ordered) {
    if (last && r.boot !== last.boot) {
      if (active) { issue(active, 'reboot'); close(r, 'reboot'); }
      qualityBad = false; inputPowered = undefined; protectedBattery = false;
    }
    last = r;
    if (r.event === 104 && r.data.length === 10) {
      qualityBad = !(r.data[2] & 256) || !!(r.data[2] & 512) || (r.data[2] & 255) !== 1;
      if (active && qualityBad) issue(active, r.data[2] & 512 ? 'sample_gap' : 'invalid_sample');
    }
    if (r.event === 110 && r.data.length === 10) {
      inputPowered = !!(r.data[2] & 1);
      if (inputPowered) close(r, 'external_power');
      else if (!active && r.data[6] && r.data[7] < 2 && !protectedBattery) begin(r);
    }
    if (r.event === 101 && r.data.length === 10) {
      if (!active && inputPowered !== true && !protectedBattery) begin(r);
      // Prefer the explicit start snapshot over the input event from this tick.
      else if (active && duration(active.start, r) === 0) {
        active.start = r; active.startSocPercent = snapshotSoc(r);
      }
    }
    if (r.event === 111 && r.data.length === 10 && (r.data[2] & 255) === 1) close(r, 'charging');
    if (active) {
      if (r.pending) issue(active, 'pending_records');
      if (r.event === 103) issue(active, 'configuration_changed');
      if (r.data.length === 10 && !r.data[6]) issue(active, 'invalid_sample');
      if (r.event === 106 && r.data[2] === 1 && !low) {
        low = r; active.milestones.toLowMs = duration(active.start, low);
      }
      // Some policy events are enqueued before Tick observes the new input.
      // The first severe/protection event freezes the discharge endpoint.
      if (r.event === 108 || (r.event === 106 && r.data[2] >= 2)) close(r, 'protection');
      if (r.event === 114) {
        const reasons: BatteryCycleEnd[] = ['unknown', 'unknown', 'manual', 'timeout', 'reboot', 'storage_failure'];
        close(r, reasons[r.data[2]] ?? 'unknown');
      }
    }
    if (r.event === 108 || (r.event === 106 && r.data[2] >= 2)) protectedBattery = true;
    if (r.event === 106 && r.data[2] < 2) protectedBattery = false;
  }
  if (active && last) active.elapsedMs = duration(active.start, last);
  const start = ordered.find(r => r.event === 100 && r.data.length === 14);
  const end = ordered.find(r => r.event === 114 && r.data.length >= 9);
  const missing = missingSequences(ordered);
  for (const cycle of cycles) {
    if (!start) issue(cycle, 'unknown_start');
    if (coverage.more || (coverage.status && coverage.status !== 'ok') || coverage.overwritten)
      issue(cycle, 'partial_log');
    if (missing || coverage.dropped || (end && (end.data[7] || end.data[8]))) issue(cycle, 'missing_records');
    const to = cycle.elapsedMs;
    for (const r of ordered) {
      const at = duration(cycle.start, r);
      if (at === null || to === null) continue;
      if (r.pending && at <= to) issue(cycle, 'pending_records');
      if (r.event === 102 && r.data.length === 14) {
        const bucketStart = (r.data[2] - cycle.start.uptimeMs) | 0;
        const bucketEnd = (r.data[3] - cycle.start.uptimeMs) | 0;
        if (bucketEnd >= 0 && bucketStart <= to) {
          if (r.data[9] || !r.data[8]) issue(cycle, 'sample_gap');
          if (r.data[13]) issue(cycle, 'missing_records');
        }
      }
    }
    cycle.status = cycle.issues.length ? 'unverified' : cycle.endReason === 'protection' ? 'valid' :
      cycle.endReason === 'ongoing' ? 'recording' : 'interrupted';
    cycle.enduranceMs = cycle.status === 'valid' ? cycle.elapsedMs : null;
  }
  return cycles;
}

export function batterySessions(records: FaultRecord[], coverage: BatteryLogCoverage = {}) {
  const supported = records.filter(r => r.event >= 100 && r.event <= 114 && r.data[0] === 1 && r.data.length >= 2);
  const ids = [...new Set(supported.map(r => r.data[1]))];
  return ids.map(id => {
    const items = supported.filter(r => r.data[1] === id);
    const start = items.find(r => r.event === 100);
    const end = items.find(r => r.event === 114);
    const trend = items.filter(r => r.event === 102 && r.data.length === 14)
      .sort((a, b) => a.sequence - b.sequence);
    const ordered = [...items];
    // Pending records have sequence=0 and arrive in producer queue order.
    ordered.sort((a, b) => Number(a.pending) - Number(b.pending) || (a.pending ? 0 : a.sequence - b.sequence));
    const cycles = dischargeCycles(ordered, coverage);
    const protection = ordered.find(r => r.event === 108);
    const plugged = protection && ordered.find(r => r.sequence > protection.sequence && r.event === 110 && (r.data[2] & 1));
    const ready = plugged && ordered.find(r => r.sequence > plugged.sequence && r.event === 106 && r.data[2] === 3);
    const onlyCycle = cycles.length === 1 && cycles[0].status === 'valid' ? cycles[0] : undefined;
    return { id, start, end, trend, ended: !!end,
      incomplete: !start || !end || end.data[8] !== 0 || !!end.data[7] || missingSequences(ordered) || !!coverage.more ||
        !!coverage.overwritten || !!coverage.dropped || !!(coverage.status && coverage.status !== 'ok') || items.some(r => r.pending),
      cycles,
      milestones: { toLowMs: onlyCycle?.milestones.toLowMs ?? null, toProtectionMs: onlyCycle?.milestones.toProtectionMs ?? null,
        afterWarningMs: onlyCycle?.milestones.afterWarningMs ?? null, chargingToReadyMs: duration(plugged || undefined, ready || undefined) },
      records: ordered.map(r => ({ sequence: r.sequence, uptimeMs: r.uptimeMs, pending: r.pending, ...decodeBatteryRecord(r) })) };
  });
}
