export interface DeviceInitializationLoaders<TLayout> {
  globalConfig: () => Promise<void>;
  screenControl: () => Promise<void>;
  profileList: () => Promise<void>;
  hotkeys: () => Promise<void>;
  firmwareMetadata: () => Promise<void>;
  hitboxLayout: () => Promise<TLayout>;
}

export interface DeviceInitializationOptions<TLayout> {
  loaders: DeviceInitializationLoaders<TLayout>;
  isCurrent: () => boolean;
  onReady: (layout: TLayout) => void;
  onFailure: (error: unknown) => void;
  /** One deadline for all six stages, not six independent timeouts. */
  timeoutMs?: number;
  /** Shared absolute startup deadline, beginning before HID discovery. */
  deadlineAtMs?: number;
  signal?: AbortSignal;
  onStage?: (
    stage: DeviceInitializationStage,
    status: 'started' | 'completed',
  ) => void;
}

export type DeviceInitializationResult = 'ready' | 'failed' | 'stale';
export type DeviceInitializationStage =
  | 'global-config'
  | 'screen-control'
  | 'profile-list'
  | 'hotkeys'
  | 'firmware-metadata'
  | 'hitbox-layout';

const DEFAULT_INITIALIZATION_TIMEOUT_MS = 30_000;

export class DeviceInitializationError extends Error {
  constructor(
    message: string,
    public readonly stage: DeviceInitializationStage,
    public readonly cause?: unknown,
  ) {
    super(message);
    this.name = 'DeviceInitializationError';
  }
}

export async function initializeDeviceSession<TLayout>(
  options: DeviceInitializationOptions<TLayout>,
): Promise<DeviceInitializationResult> {
  const timeoutMs = options.timeoutMs ?? DEFAULT_INITIALIZATION_TIMEOUT_MS;
  const deadline = options.deadlineAtMs ?? (Date.now() + timeoutMs);
  if (
    !Number.isFinite(deadline) ||
    (options.deadlineAtMs === undefined && (!Number.isFinite(timeoutMs) || timeoutMs <= 0))
  ) {
    throw new TypeError('Device initialization deadline must be finite and positive');
  }
  let currentStage: DeviceInitializationStage = 'global-config';

  const run = async <T>(
    stage: DeviceInitializationStage,
    loader: () => Promise<T>,
  ): Promise<T> => {
    currentStage = stage;
    assertInitializationActive(options, stage);
    options.onStage?.(stage, 'started');
    const value = await awaitInitializationStage(
      loader(),
      deadline,
      options.signal,
      stage,
    );
    assertInitializationActive(options, stage);
    options.onStage?.(stage, 'completed');
    return value;
  };

  try {
    // The HID transport owns a single physical writer. Starting six promises
    // at once only hides which command is blocking and lets telemetry compete
    // with startup. Keep the order explicit and diagnostically observable.
    await run('global-config', options.loaders.globalConfig);
    await run('screen-control', options.loaders.screenControl);
    await run('profile-list', options.loaders.profileList);
    await run('hotkeys', options.loaders.hotkeys);
    await run('firmware-metadata', options.loaders.firmwareMetadata);
    const layout = await run('hitbox-layout', options.loaders.hitboxLayout);
    if (!options.isCurrent()) return 'stale';
    options.onReady(layout);
    return 'ready';
  } catch (error) {
    if (!options.isCurrent() || options.signal?.aborted) return 'stale';
    const reported = error instanceof DeviceInitializationError
      ? error
      : new DeviceInitializationError(
        `Device initialization failed during ${currentStage}`,
        currentStage,
        error,
      );
    options.onFailure(reported);
    return 'failed';
  }
}

function assertInitializationActive<TLayout>(
  options: DeviceInitializationOptions<TLayout>,
  stage: DeviceInitializationStage,
): void {
  if (!options.isCurrent() || options.signal?.aborted) {
    throw new DeviceInitializationError(
      `Device initialization was cancelled during ${stage}`,
      stage,
      options.signal?.reason,
    );
  }
}

function awaitInitializationStage<T>(
  operation: Promise<T>,
  deadline: number,
  signal: AbortSignal | undefined,
  stage: DeviceInitializationStage,
): Promise<T> {
  if (signal?.aborted) {
    return Promise.reject(new DeviceInitializationError(
      `Device initialization was cancelled during ${stage}`,
      stage,
      signal.reason,
    ));
  }
  const remaining = Math.max(0, deadline - Date.now());
  return new Promise<T>((resolve, reject) => {
    let settled = false;
    const finish = (callback: () => void): void => {
      if (settled) return;
      settled = true;
      clearTimeout(timeout);
      signal?.removeEventListener('abort', abort);
      callback();
    };
    const abort = (): void => finish(() => reject(new DeviceInitializationError(
      `Device initialization was cancelled during ${stage}`,
      stage,
      signal?.reason,
    )));
    const timeout = setTimeout(() => finish(() => reject(new DeviceInitializationError(
      `Device initialization timed out during ${stage}`,
      stage,
    ))), remaining);
    signal?.addEventListener('abort', abort, { once: true });
    // Observing both outcomes prevents a loader which settles after the
    // deadline from creating an unhandled rejection.
    operation.then(
      (value) => finish(() => resolve(value)),
      (error) => finish(() => reject(new DeviceInitializationError(
        `Device initialization failed during ${stage}`,
        stage,
        error,
      ))),
    );
  });
}
