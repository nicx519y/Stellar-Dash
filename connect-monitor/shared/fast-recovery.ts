export interface FastRequest {
  operation: 1 | 2 | 3 | 4 | 5;
  target?: 0 | 1 | 2;
  kind?: number;
  testId: number;
  count?: number;
  durationMs?: number;
  channel?: number;
  enabled?: boolean;
  delayUs?: number;
  seed?: number;
}
export interface FastStatus {
  role: number; testId: number; profile: number; state: number;
  requested: boolean; effective: boolean; reason: number; supportedRates: number;
  acceptedRates: number; sequence: number; successes: number; failures: number;
  elapsedUs: number; overflow: number;
}
export interface FastEvent {
  role: number; sequence: number; testId: number; event: number; channel: number;
  atUs: number; plannedUs: number; value: number; generation: number; maintenanceUs: number;
}
export const fastStates = ["关闭", "配置协商", "预约生效", "等待边界", "验证模式", "已启用（实验）", "原频道重试", "频道会合", "暂留候选", "提交确认", "已降级"];
export const fastReasons = ["无", "定时迟到", "射频失败", "建立超时", "模式协商超时", "恢复超时", "速率不支持", "用户停止/租约到期", "链路重置"];
export function percentile(values: number[], q: number): number | null {
  if (!values.length) return null;
  const sorted = [...values].sort((a,b)=>a-b);
  return sorted[Math.max(0,Math.ceil(q*sorted.length)-1)];
}
