export type ClearableCardKey = "latency";

const STORAGE_PREFIX = "connect-monitor:card-clear:";
const CHANNEL_NAME = "connect-monitor-card-clear";

function storageKey(key: ClearableCardKey) {
  return `${STORAGE_PREFIX}${key}`;
}

export function readCardClearAfter(key: ClearableCardKey): number {
  try {
    const value = Number(window.localStorage.getItem(storageKey(key)));
    return Number.isFinite(value) ? value : 0;
  } catch {
    return 0;
  }
}

export function publishCardClear(key: ClearableCardKey, timestampMs = Date.now()): number {
  try {
    window.localStorage.setItem(storageKey(key), String(timestampMs));
  } catch {
  }

  try {
    const channel = new BroadcastChannel(CHANNEL_NAME);
    channel.postMessage({ key, timestampMs });
    channel.close();
  } catch {
  }

  window.dispatchEvent(new CustomEvent(CHANNEL_NAME, { detail: { key, timestampMs } }));
  return timestampMs;
}

export function subscribeCardClear(key: ClearableCardKey, handler: (timestampMs: number) => void): () => void {
  const onStorage = (event: StorageEvent) => {
    if (event.key !== storageKey(key)) return;
    const timestampMs = Number(event.newValue);
    if (Number.isFinite(timestampMs)) {
      handler(timestampMs);
    }
  };
  const onWindowEvent = (event: Event) => {
    const detail = (event as CustomEvent<{ key?: string; timestampMs?: number }>).detail;
    if (detail?.key === key && typeof detail.timestampMs === "number") {
      handler(detail.timestampMs);
    }
  };

  let channel: BroadcastChannel | null = null;
  try {
    channel = new BroadcastChannel(CHANNEL_NAME);
    channel.onmessage = (event: MessageEvent<{ key?: string; timestampMs?: number }>) => {
      if (event.data?.key === key && typeof event.data.timestampMs === "number") {
        handler(event.data.timestampMs);
      }
    };
  } catch {
  }

  window.addEventListener("storage", onStorage);
  window.addEventListener(CHANNEL_NAME, onWindowEvent);
  return () => {
    window.removeEventListener("storage", onStorage);
    window.removeEventListener(CHANNEL_NAME, onWindowEvent);
    channel?.close();
  };
}
