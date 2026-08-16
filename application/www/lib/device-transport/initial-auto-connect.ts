import { DeviceTransport } from './types';

export const WEBHID_CROSS_DOCUMENT_HANDOFF_GRACE_MS = 250;

type Schedule = (
  callback: () => void,
  delayMs: number,
) => ReturnType<typeof setTimeout>;

type Cancel = (timer: ReturnType<typeof setTimeout>) => void;

interface PageTransitionEventLike {
  readonly persisted?: boolean;
}

interface PageLifecycleTarget {
  addEventListener(
    type: 'pagehide' | 'pageshow',
    listener: (event: PageTransitionEventLike) => void,
  ): void;
  removeEventListener(
    type: 'pagehide' | 'pageshow',
    listener: (event: PageTransitionEventLike) => void,
  ): void;
}

export interface DevicePageLifecycleCallbacks {
  suspendForBfcache(): void;
  destroyDocument(): void;
  restoreFromBfcache(): void;
}

/**
 * Release the physical HID handle on every pagehide. A bfcache document keeps
 * its JS client but reconnects explicitly after pageshow; a terminal document
 * disposes it. Neither path sends session.end or another protocol message.
 */
export function registerDevicePageLifecycle(
  callbacks: DevicePageLifecycleCallbacks,
  target: PageLifecycleTarget = window,
): () => void {
  let hidden = false;
  let terminal = false;
  const handlePageHide = (event: PageTransitionEventLike): void => {
    if (hidden || terminal) return;
    hidden = true;
    if (event.persisted) callbacks.suspendForBfcache();
    else {
      terminal = true;
      callbacks.destroyDocument();
    }
  };
  const handlePageShow = (event: PageTransitionEventLike): void => {
    if (!event.persisted || !hidden || terminal) return;
    hidden = false;
    callbacks.restoreFromBfcache();
  };
  target.addEventListener('pagehide', handlePageHide);
  target.addEventListener('pageshow', handlePageShow);
  return () => {
    target.removeEventListener('pagehide', handlePageHide);
    target.removeEventListener('pageshow', handlePageShow);
  };
}

/**
 * Give the previous document's bounded HID close enough time to release the
 * Windows handle before this document performs its first chooser-free open.
 * Mock mode deliberately stays synchronous with its existing lifecycle.
 */
export function scheduleInitialDeviceAutoConnect(
  transportKind: DeviceTransport['kind'],
  closeTimeoutMs: number,
  connect: () => Promise<void>,
  onFailure: (error: unknown) => void,
  schedule: Schedule = setTimeout,
  cancel: Cancel = clearTimeout,
): () => void {
  if (transportKind !== 'webhid') {
    void connect().catch(onFailure);
    return () => undefined;
  }
  if (!Number.isFinite(closeTimeoutMs) || closeTimeoutMs < 0) {
    throw new RangeError('WebHID close timeout must be a non-negative number');
  }

  let active = true;
  const timer = schedule(() => {
    if (!active) return;
    active = false;
    void connect().catch(onFailure);
  }, closeTimeoutMs + WEBHID_CROSS_DOCUMENT_HANDOFF_GRACE_MS);

  return () => {
    if (!active) return;
    active = false;
    cancel(timer);
  };
}
