declare module '@hbox/device-transport-runtime' {
  export const BUILD_DEVICE_TRANSPORT_MODE: 'webhid' | 'mock';

  export function createBuildDeviceCommandClient(
    config: import('@/lib/device-transport/device-command-types').DeviceTransportConfig,
  ): import('@/lib/device-transport/device-command-client').DeviceCommandClient;
}

declare module '@hbox/build-variant-badge' {
  export function BuildVariantBadge(): import('react').ReactElement | null;
}
