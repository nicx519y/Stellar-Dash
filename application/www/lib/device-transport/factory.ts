import { DeviceCommandClient } from './device-command-client';
import {
  DEFAULT_DEVICE_TRANSPORT_CONFIG,
  DeviceTransportConfig,
} from './device-command-types';
import {
  BUILD_DEVICE_TRANSPORT_MODE,
  createBuildDeviceCommandClient,
} from '@hbox/device-transport-runtime';

export type DeviceTransportMode = 'webhid' | 'mock';

export interface DeviceTransportFactoryOptions {
  mode: DeviceTransportMode;
  config?: Partial<DeviceTransportConfig>;
}

export function createDeviceCommandClient(
  options: DeviceTransportFactoryOptions,
): DeviceCommandClient {
  if (options.mode !== BUILD_DEVICE_TRANSPORT_MODE) {
    throw new Error(
      `Device transport "${options.mode}" is unavailable in this ${BUILD_DEVICE_TRANSPORT_MODE} build`,
    );
  }
  const config = { ...DEFAULT_DEVICE_TRANSPORT_CONFIG, ...options.config };
  return createBuildDeviceCommandClient(config);
}

export function configuredTransportMode(): DeviceTransportMode {
  return BUILD_DEVICE_TRANSPORT_MODE;
}
