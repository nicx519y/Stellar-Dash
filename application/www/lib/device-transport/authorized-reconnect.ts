export const FIRMWARE_RECONNECT_DELAY_MS = 3_000;

type Schedule = (
  callback: () => void,
  delayMs: number,
) => ReturnType<typeof setTimeout>;

type Cancel = (timer: ReturnType<typeof setTimeout>) => void;

export function scheduleAuthorizedReconnect(
  reconnect: () => Promise<void>,
  onFailure: (error: unknown) => void,
  schedule: Schedule = setTimeout,
  cancel: Cancel = clearTimeout,
): () => void {
  const timer = schedule(() => {
    void reconnect().catch(onFailure);
  }, FIRMWARE_RECONNECT_DELAY_MS);
  return () => cancel(timer);
}
