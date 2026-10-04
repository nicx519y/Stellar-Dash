import type { PublicFirmwareRelease } from '../admin/firmware-types';
import type { FirmwareInventory, ReleaseProgress } from './release-install-client';
import { accumulateInstallProgress } from './release-install-progress';

export const INSTALL_TASK_KEY = 'xora-release-install-v2';
export const INSTALL_TIMEOUT_MS = 180_000;
export type InstallResult = 'waiting' | 'timeout' | 'completed' | 'restored' | 'restore-failed' | 'failed';
export interface InstallTask {
  protocol: 2; sessionId: string; digest: string; version: string; activatedAt: number;
  release: PublicFirmwareRelease; result: InstallResult; error?: string; progress?: ReleaseProgress;
}
export function readInstallTask(storage: Pick<Storage, 'getItem'> = localStorage): InstallTask | null {
  try {
    const t = JSON.parse(storage.getItem(INSTALL_TASK_KEY) || 'null') as InstallTask | null;
    return t && t.protocol === 2 && /^rel-[a-zA-Z0-9-]{1,28}$/.test(t.sessionId) && /^[a-f0-9]{64}$/.test(t.digest)
      && Number.isFinite(t.activatedAt) && t.activatedAt > 0 && t.version === t.release?.manifest.version
      && ['waiting', 'timeout', 'completed', 'restored', 'restore-failed', 'failed'].includes(t.result) ? t : null;
  } catch { return null; }
}
export function saveInstallTask(t: InstallTask, storage: Pick<Storage, 'setItem' | 'getItem'> = localStorage): void {
  const json = JSON.stringify(t); storage.setItem(INSTALL_TASK_KEY, json);
  if (storage.getItem(INSTALL_TASK_KEY) !== json) throw new Error('Cannot persist installation task');
}
// Completed installations remain visible in the current dialog, but are not
// work to resume when the firmware page is opened again. Also retire old records.
export function readResumableInstallTask(storage: Storage = localStorage): InstallTask | null {
  const task = readInstallTask(storage);
  if (task?.result !== 'completed') return task;
  try { forgetInstallTask(task.sessionId, storage); } catch { /* Ignore a completed record even if storage is read-only. */ }
  return null;
}
export function forgetInstallTask(sessionId: string, storage: Storage = localStorage): void {
  if (readInstallTask(storage)?.sessionId === sessionId) storage.removeItem(INSTALL_TASK_KEY);
}
export function matchesInstallTask(t: InstallTask, i: FirmwareInventory): boolean {
  return i.protocol === 2 && i.sessionId === t.sessionId && i.targetDigest === t.digest && i.targetVersion === t.version;
}
export function reconcileInstallTask(t: InstallTask, i: FirmwareInventory): InstallTask | null {
  if (!matchesInstallTask(t, i)) return null;
  const previous = t.progress || { stage: 'waiting-device', overallPercent: 80, stepIndex: 6 };
  if (i.phase === 'completed' && i.installationState === 'installed' && i.confirmedDigest === t.digest && i.confirmedVersion === t.version)
    t = { ...t, result: 'completed', error: '' };
  else if (i.phase === 'restored' && i.recoveryResult === 'restored') t = { ...t, result: 'restored', error: i.installError || i.error };
  else if (i.phase === 'restore-failed' || i.recoveryResult === 'failed') t = { ...t, result: 'restore-failed', error: i.recoveryError || i.error };
  else if (i.phase === 'failed') t = { ...t, result: 'failed', error: i.error };
  const stage = t.result === 'waiting' ? (i.phase === 'completed' ? 'verifying' : i.phase) : t.result;
  return { ...t, progress: accumulateInstallProgress(previous, { stage }) };
}

// One operation at a time, including across UI renders. Timeout only stops observation.
export function createInstallMonitor(options: {
  task: InstallTask; connected: () => boolean; reconnect: () => Promise<unknown>;
  waitReady?: () => Promise<void>; query: () => Promise<FirmwareInventory>; changed: (task: InstallTask, inventory?: FirmwareInventory) => void;
  now?: () => number; schedule?: (fn: () => void, delay: number) => ReturnType<typeof setTimeout>;
  cancel?: (id: ReturnType<typeof setTimeout>) => void;
}) {
  const now = options.now || Date.now, schedule = options.schedule || setTimeout, cancel = options.cancel || clearTimeout;
  let task = options.task, disposed = false, allowLateRead = false, inFlight: Promise<void> | null = null;
  let timer: ReturnType<typeof setTimeout> | undefined, deadline: ReturnType<typeof setTimeout> | undefined;
  const clear = () => { if (timer !== undefined) cancel(timer); if (deadline !== undefined) cancel(deadline); timer = deadline = undefined; };
  const timeout = () => {
    if (disposed || task.result !== 'waiting') return;
    task = { ...task, result: 'timeout', progress: accumulateInstallProgress(task.progress || { stage: 'waiting-device', overallPercent: 80, stepIndex: 6 }, { stage: 'timeout' }) }; clear(); options.changed(task);
  };
  const run = (authorize?: () => Promise<unknown>, manual = false): Promise<void> => {
    if (disposed) return Promise.resolve();
    if (inFlight) { if (manual) allowLateRead = true; return inFlight; }
    allowLateRead = manual;
    inFlight = (async () => {
      try {
        if (authorize) await authorize();
        else if (!options.connected()) await options.reconnect();
        if (disposed || !options.connected()) return;
        await options.waitReady?.();
        if (disposed || !options.connected() || (task.result === 'timeout' && !allowLateRead)) return;
        const i = await options.query(); if (disposed) return;
        const next = reconcileInstallTask(task, i);
        if (next) { task = next; options.changed(task, i); }
      } catch { /* A disconnected TX is expected; deadline remains independent of request latency. */ }
      finally {
        inFlight = null;
        if (!disposed && task.result === 'waiting') {
          if (now() - task.activatedAt >= INSTALL_TIMEOUT_MS) timeout();
          else timer = schedule(() => { timer = undefined; void run(); }, options.connected() ? 2000 : 3000);
        } else clear();
      }
    })();
    return inFlight;
  };
  return {
    start() {
      if (task.result !== 'waiting' || disposed) return;
      if (now() - task.activatedAt >= INSTALL_TIMEOUT_MS) { timeout(); return; }
      deadline = schedule(timeout, INSTALL_TIMEOUT_MS - (now() - task.activatedAt));
      timer = schedule(() => { timer = undefined; void run(); }, options.connected() ? 2000 : 3000);
    },
    manual(authorize?: () => Promise<unknown>) { if (timer !== undefined) { cancel(timer); timer = undefined; } return run(authorize, true); },
    stop() { disposed = true; clear(); },
  };
}
