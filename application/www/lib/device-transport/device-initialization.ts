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
}

export type DeviceInitializationResult = 'ready' | 'failed' | 'stale';

export async function initializeDeviceSession<TLayout>(
  options: DeviceInitializationOptions<TLayout>,
): Promise<DeviceInitializationResult> {
  try {
    const [, , , , , layout] = await Promise.all([
      options.loaders.globalConfig(),
      options.loaders.screenControl(),
      options.loaders.profileList(),
      options.loaders.hotkeys(),
      options.loaders.firmwareMetadata(),
      options.loaders.hitboxLayout(),
    ]);
    if (!options.isCurrent()) return 'stale';
    options.onReady(layout);
    return 'ready';
  } catch (error) {
    if (!options.isCurrent()) return 'stale';
    options.onFailure(error);
    return 'failed';
  }
}
