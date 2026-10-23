import type { FaultRecord } from './device-fault-logs';

/** Preserve gaps rather than drawing an apparently continuous measurement. */
export function batteryTrendChart(trend: FaultRecord[], start?: FaultRecord) {
  const origin = start?.uptimeMs ?? trend[0]?.data[2] ?? 0;
  const points = trend.map(record => ({
    x: (((record.data[3] - origin) >>> 0) / 3600000),
    valid: record.data[8] > 0 && record.data[9] === 0,
    data: record.data,
  }));
  return {
    voltage: points.map(p => ({ x: p.x, y: p.valid ? p.data[4] / 1000 : null })),
    soc: points.map(p => ({ x: p.x, y: p.valid ? p.data[7] / 10 : null })),
    maxHours: Math.max(1 / 3600000, ...points.map(p => p.x)),
  };
}
