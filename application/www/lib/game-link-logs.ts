import type { FaultLogPage, FaultRecord } from './device-fault-logs';

const reasons = [
  ['USB 连接丢失', 'USB connection lost'], ['USB 总线复位', 'USB bus reset'],
  ['USB 挂起', 'USB suspended'], ['报告完成停滞', 'Report completion stalled'],
  ['输入停止更新', 'Input stopped updating'], ['输出拥塞', 'Output congestion'],
  ['报告速率降级', 'Report rate fallback'], ['板间通信异常', 'Board link fault'],
  ['RF 连接丢失', 'RF connection lost'], ['无线收发异常', 'Radio fault'],
  ['触发中立报告', 'Neutral report requested'], ['恢复失败', 'Recovery failed'],
  ['启动或枚举超时', 'Startup or enumeration timeout'], ['输入帧异常', 'Invalid input frame'],
  ['电源保护中断', 'Power protection interruption'], ['中立报告完成', 'Neutral report completed'],
  ['已确认断链', 'Confirmed disconnection'],
] as const;
const events = [
  ['连接异常开始', 'Connection incident began'], ['连接异常变化', 'Connection incident changed'],
  ['连接已恢复', 'Connection recovered'], ['连接现场信息', 'Connection context'],
  ['连接恢复操作', 'Connection recovery action'],
] as const;
export function decodeGameLink(record: FaultRecord) {
  const d = record.data;
  if (record.event < 200 || record.event > 204 || d.length !== 14 || d[0] !== 1 || ![1, 2].includes(d[2])) return null;
  return { incident: d[1], mode: d[2] === 1 ? 'USB' : 'RF', reasons: d[3], startMs: d[4],
    durationMs: d[5], status: d[6], received: d[7], completed: d[8], dropped: d[9],
    inputGapMs: d[10], completionGapMs: d[11], rateHz: d[12] & 0xffff,
    profile: d[12] >>> 16, detail: d[13] };
}
export function gameLinkText(record: FaultRecord, language: 'zh' | 'en') {
  const d = decodeGameLink(record); if (!d) return null;
  const index = language === 'zh' ? 0 : 1;
  const names: string[] = reasons.flatMap((labels, bit) => d.reasons & (1 << bit) ? [labels[index]] : []);
  if (d.reasons >>> reasons.length) names.push(`0x${(d.reasons >>> reasons.length << reasons.length).toString(16)}`);
  return { name: events[record.event - 200][index], description: `${d.mode} · #${d.incident} · ${names.join(' / ')} · ${d.durationMs} ms`,
    details: language === 'zh' ? `输入间隔 ${d.inputGapMs} ms · 完成间隔 ${d.completionGapMs} ms · 收到 ${d.received} · 完成 ${d.completed} · 丢弃 ${d.dropped} · ${d.rateHz} Hz` :
      `Input gap ${d.inputGapMs} ms · Completion gap ${d.completionGapMs} ms · Received ${d.received} · Completed ${d.completed} · Dropped ${d.dropped} · ${d.rateHz} Hz` };
}
export function gameLinkIncidents(pages: FaultLogPage[]) {
  const groups = new Map<string, { source: string; boot: number; incident: number; mode: string;
    began: boolean; recovered: boolean; reasons: number; durationMs: number; pending: boolean; incomplete: boolean }>();
  for (const p of pages) for (const r of p.items) {
    const d = decodeGameLink(r); if (!d || r.event === 203 || r.event === 204) continue;
    const key = `${p.source}:${r.boot}:${d.mode}:${d.incident}`;
    const g = groups.get(key) ?? { source: p.source, boot: r.boot, incident: d.incident, mode: d.mode,
      began: false, recovered: false, reasons: 0, durationMs: 0, pending: false, incomplete: false };
    g.began ||= r.event === 200; g.recovered ||= r.event === 202; g.reasons |= d.reasons;
    g.durationMs = Math.max(g.durationMs, d.durationMs); g.pending ||= r.pending;
    g.incomplete ||= p.status !== 'ok' || p.more || p.overwritten || p.dropped > 0;
    groups.set(key, g);
  }
  return [...groups.values()].map(g => ({ ...g, incomplete: g.incomplete || !g.began || !g.recovered }));
}
