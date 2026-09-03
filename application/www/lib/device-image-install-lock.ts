import { useSyncExternalStore } from 'react';

export type DeviceImageInstallState = { imageId: string; progress: number } | null;

let current: DeviceImageInstallState = null;
const listeners = new Set<() => void>();

function emit() {
  listeners.forEach(listener => listener());
}

export function beginDeviceImageInstall(imageId: string): boolean {
  if (current) return false;
  current = { imageId, progress: 0 };
  emit();
  return true;
}

export function updateDeviceImageInstall(imageId: string, progress: number) {
  if (!current || current.imageId !== imageId) return;
  const next = Math.max(current.progress, Math.max(0, Math.min(100, Math.floor(progress))));
  if (next === current.progress) return;
  current = { imageId, progress: next };
  emit();
}

export function finishDeviceImageInstall(imageId: string) {
  if (!current || current.imageId !== imageId) return;
  current = null;
  emit();
}

export function getDeviceImageInstallState(): DeviceImageInstallState {
  return current;
}

export function deviceImageInstallRingProgress(progress: number): number {
  const completed = Math.max(0, Math.min(100, Number.isFinite(progress) ? progress : 0));
  return 1 - completed / 100;
}

export function useDeviceImageInstallState(): DeviceImageInstallState {
  return useSyncExternalStore(
    listener => { listeners.add(listener); return () => listeners.delete(listener); },
    getDeviceImageInstallState,
    () => null,
  );
}
