export const HOLD_PROGRESS_DELAY_MS = 500;
export const HOLD_PROGRESS_DURATION_MS = 1500;
export const HOLD_TO_INSTALL_MS = HOLD_PROGRESS_DELAY_MS + HOLD_PROGRESS_DURATION_MS;

export type HoldGestureProgress = {
  progress: number;
  delayMs: number;
};

export function advanceHoldProgress(current: number, pressed: boolean, elapsedMs: number): number {
  const direction = pressed ? 1 : -1;
  return Math.max(0, Math.min(1, current + direction * Math.max(0, elapsedMs) / HOLD_PROGRESS_DURATION_MS));
}

export function advanceHoldGesture(
  current: HoldGestureProgress,
  pressed: boolean,
  elapsedMs: number,
): HoldGestureProgress {
  const elapsed = Math.max(0, elapsedMs);
  if (!pressed) {
    return { progress: advanceHoldProgress(current.progress, false, elapsed), delayMs: 0 };
  }
  // A resumed hold keeps the remaining progress and does not pay the delay twice.
  if (current.progress > 0) {
    return { progress: advanceHoldProgress(current.progress, true, elapsed), delayMs: HOLD_PROGRESS_DELAY_MS };
  }
  const accumulatedDelay = current.delayMs + elapsed;
  const progressElapsed = Math.max(0, accumulatedDelay - HOLD_PROGRESS_DELAY_MS);
  return {
    progress: advanceHoldProgress(0, true, progressElapsed),
    delayMs: Math.min(HOLD_PROGRESS_DELAY_MS, accumulatedDelay),
  };
}

export function isShortSelectionPress(elapsedMs: number, resumed: boolean): boolean {
  return !resumed && elapsedMs >= 0 && elapsedMs < HOLD_PROGRESS_DELAY_MS;
}
