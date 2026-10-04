import type { ReleaseProgress } from './release-install-client';

// Measured workflow progress. Offline estimates belong to presentation only;
// they must never be persisted as a device milestone or installation result.
export const INSTALL_STEPS = [
  { stage: 'downloading', start: 0, end: 10, zh: '下载', en: 'Download' },
  { stage: 'extracting', start: 10, end: 15, zh: '解压与校验', en: 'Extract and verify' },
  { stage: 'backup', start: 15, end: 20, zh: '备份配置', en: 'Back up configuration' },
  { stage: 'backing-up-tx', start: 20, end: 40, zh: '备份当前 TX', en: 'Back up current TX' },
  { stage: 'staging-controller', start: 40, end: 70, zh: '写入主控备用槽', en: 'Write inactive controller slot' },
  { stage: 'staging-tx', start: 70, end: 80, zh: '暂存新 TX', en: 'Stage new TX' },
  { stage: 'tx-writing', start: 80, end: 90, zh: '离线安装 TX', en: 'Install TX offline' },
  { stage: 'committing', start: 90, end: 95, zh: '切换主控', en: 'Switch controller' },
  { stage: 'verifying', start: 95, end: 100, zh: '重连核验', en: 'Reconnect and verify' },
] as const;

// Presentation groups the detailed device milestones without changing their
// progress weights or the transaction/recovery protocol.
export const INSTALL_DISPLAY_STEPS = [
  { zh: '下载和校验', en: 'Download & verify' },
  { zh: '备份固件和配置', en: 'Back up firmware & settings' },
  { zh: '安装升级', en: 'Install update' },
] as const;

export function installDisplayStep(progress: ReleaseProgress | null): number {
  const step = accumulateInstallProgress(null, progress || { stage: 'downloading' }).stepIndex ?? 0;
  return step < 2 ? 0 : step < 4 ? 1 : 2;
}

const estimatedStages = new Set(['activating', 'activated', 'waiting-device', 'tx-writing', 'tx-verified', 'committing', 'verifying']);
export function installUsesEstimate(progress: ReleaseProgress | null, activatedAt?: number): boolean {
  return Boolean(activatedAt && Number.isFinite(activatedAt) && activatedAt > 0 && estimatedStages.has(progress?.stage || ''));
}

// Until device telemetry is available, ease through the final installation
// segment (about one minute). Leave headroom for verified completion and the
// independent 180s timeout. Failure/recovery freezes the last displayed value.
export function displayedInstallPercent(progress: ReleaseProgress | null, activatedAt: number | undefined,
  now: number, previous = 0): number {
  const measured = accumulateInstallProgress(null, progress || { stage: 'downloading' }).overallPercent!;
  if (progress?.stage === 'completed') return 100;
  const elapsed = activatedAt && Number.isFinite(now) ? Math.max(0, now - activatedAt) : 0;
  const estimate = installUsesEstimate(progress, activatedAt) ? 80 + 18 * (1 - Math.exp(-elapsed / 27_500)) : 0;
  return Math.min(99, Math.floor(Math.max(measured, estimate, Number.isFinite(previous) ? previous : 0)));
}

const aliases: Record<string, string> = {
  declaring: 'backing-up-tx', prepared: 'tx-writing', activating: 'tx-writing',
  activated: 'tx-writing', 'waiting-device': 'tx-writing', 'tx-verified': 'committing',
};

export function accumulateInstallProgress(previous: ReleaseProgress | null, next: ReleaseProgress): ReleaseProgress {
  const step = INSTALL_STEPS.findIndex(s => s.stage === (aliases[next.stage] || next.stage));
  const allocation = INSTALL_STEPS[step];
  // Component counters remain available for diagnostics. Controller progress
  // uses the sum of all controller components so the ADC upload cannot reset it.
  const received = next.stageReceived ?? next.received;
  const total = next.stageTotal ?? next.total;
  const measurable = ['downloading', 'extracting', 'backup', 'backing-up-tx', 'staging-controller', 'staging-tx'].includes(next.stage);
  const fraction = measurable && received !== undefined && Number.isFinite(received) && total && Number.isFinite(total) && total > 0
    ? Math.max(0, Math.min(1, received / total)) : 0;
  const measured = allocation ? allocation.start + (allocation.end - allocation.start) * fraction : 0;
  const saved = Number.isFinite(next.overallPercent) ? next.overallPercent! : 0;
  const prior = Number.isFinite(previous?.overallPercent) ? previous!.overallPercent! : 0;
  const overallPercent = next.stage === 'completed' ? 100 : Math.min(99, Math.floor(Math.max(0, measured, saved, prior)));
  return { ...next, overallPercent, stepIndex: next.stage === 'completed' ? INSTALL_STEPS.length :
    Math.max(step, next.stepIndex ?? -1, previous?.stepIndex ?? -1) };
}
