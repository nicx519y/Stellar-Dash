import { DeviceTransportError } from './types';

export const WEBHID_DEVICE_LOCK_NAME = 'hbox-webhid-device';
export const WEBHID_DEVICE_LOCK_TIMEOUT_MS = 2_000;

export interface DeviceConnectionLease {
  acquire(signal?: AbortSignal): Promise<void>;
  release(): void;
}

interface WebLockLike {
  readonly name: string;
}

export interface WebLockManagerLike {
  request<T>(
    name: string,
    options: { mode: 'exclusive'; signal: AbortSignal },
    callback: (lock: WebLockLike | null) => Promise<T>,
  ): Promise<T>;
}

/** One same-origin lease held for the complete physical HID connection. */
export class WebHidDeviceLease implements DeviceConnectionLease {
  private acquireInFlight: Promise<void> | null = null;
  private waitController: AbortController | null = null;
  private releaseHold: (() => void) | null = null;
  private lockCompletion: Promise<void> = Promise.resolve();
  private releasing: Promise<void> | null = null;
  private held = false;

  constructor(
    private readonly locks: WebLockManagerLike | null,
    private readonly timeoutMs = WEBHID_DEVICE_LOCK_TIMEOUT_MS,
  ) {}

  async acquire(signal?: AbortSignal): Promise<void> {
    if (this.held && !this.releasing) return;
    if (this.acquireInFlight) return this.acquireInFlight;
    if (!this.locks) {
      throw new DeviceTransportError(
        'unsupported',
        '当前浏览器不支持 Web Locks，无法安全独占 WebHID 设备',
      );
    }
    if (!Number.isFinite(this.timeoutMs) || this.timeoutMs <= 0) {
      throw new DeviceTransportError('protocol', 'WebHID device lease timeout must be positive');
    }

    const operation = this.acquireExclusive(signal);
    this.acquireInFlight = operation;
    try {
      await operation;
    } finally {
      if (this.acquireInFlight === operation) this.acquireInFlight = null;
    }
  }

  release(): void {
    if (this.waitController && !this.held) {
      this.waitController.abort(new DeviceTransportError(
        'disconnected',
        'WebHID device lease acquisition was cancelled',
      ));
      return;
    }
    const release = this.releaseHold;
    if (!release || this.releasing) return;
    release();
    const completion = this.lockCompletion.catch(() => undefined);
    const releasing = completion.finally(() => {
      if (this.releasing === releasing) this.releasing = null;
    });
    this.releasing = releasing;
  }

  private async acquireExclusive(signal?: AbortSignal): Promise<void> {
    const locks = this.locks;
    if (!locks) {
      throw new DeviceTransportError('unsupported', 'Web Locks are unavailable');
    }
    if (this.releasing) await this.releasing;
    if (this.held) return;
    if (signal?.aborted) {
      throw new DeviceTransportError('disconnected', 'WebHID device lease acquisition was cancelled');
    }

    const controller = new AbortController();
    this.waitController = controller;
    let timedOut = false;
    let releasedByCaller = false;
    let acquiredResolve!: () => void;
    let acquiredReject!: (error: unknown) => void;
    let acquiredSettled = false;
    const acquired = new Promise<void>((resolve, reject) => {
      acquiredResolve = () => {
        if (acquiredSettled) return;
        acquiredSettled = true;
        resolve();
      };
      acquiredReject = (error) => {
        if (acquiredSettled) return;
        acquiredSettled = true;
        reject(error);
      };
    });
    let releaseResolve!: () => void;
    const hold = new Promise<void>((resolve) => { releaseResolve = resolve; });
    const abortFromCaller = (): void => {
      releasedByCaller = true;
      controller.abort(signal?.reason);
      acquiredReject(new DeviceTransportError(
        'disconnected',
        'WebHID device lease acquisition was cancelled',
      ));
    };
    signal?.addEventListener('abort', abortFromCaller, { once: true });
    const timeout = setTimeout(() => {
      timedOut = true;
      controller.abort();
      acquiredReject(new DeviceTransportError(
        'device-busy',
        '另一页面正在使用 HBox WebHID 设备，请关闭该页面后重试',
      ));
    }, this.timeoutMs);

    const request = locks.request(
      WEBHID_DEVICE_LOCK_NAME,
      { mode: 'exclusive', signal: controller.signal },
      async (lock) => {
        if (!lock) {
          throw new DeviceTransportError('device-busy', 'HBox WebHID device lease was not granted');
        }
        if (controller.signal.aborted) return;
        this.waitController = null;
        this.held = true;
        this.releaseHold = releaseResolve;
        acquiredResolve();
        await hold;
        if (this.releaseHold === releaseResolve) this.releaseHold = null;
        this.held = false;
      },
    );
    this.lockCompletion = Promise.resolve(request)
      .catch((error) => {
        if (timedOut) {
          acquiredReject(new DeviceTransportError(
            'device-busy',
            '另一页面正在使用 HBox WebHID 设备，请关闭该页面后重试',
            error,
          ));
        } else if (releasedByCaller || signal?.aborted || controller.signal.aborted) {
          const reason = controller.signal.reason;
          acquiredReject(new DeviceTransportError(
            'disconnected',
            'WebHID device lease acquisition was cancelled',
            reason ?? error,
          ));
        } else {
          acquiredReject(error);
        }
      });

    try {
      await acquired;
    } finally {
      clearTimeout(timeout);
      signal?.removeEventListener('abort', abortFromCaller);
      if (this.waitController === controller) this.waitController = null;
    }
  }
}

export function createBrowserWebHidDeviceLease(): DeviceConnectionLease {
  const manager = typeof navigator === 'undefined'
    ? null
    : (navigator.locks as unknown as WebLockManagerLike | undefined) ?? null;
  return new WebHidDeviceLease(manager);
}
