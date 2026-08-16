import { DeviceCommandClient } from './device-command-client';
import { DeviceTransportConfig } from './device-command-types';
import { MockDeviceTransport } from './mock-device-transport';

export const BUILD_DEVICE_TRANSPORT_MODE = 'mock' as const;

export function createBuildDeviceCommandClient(
  config: DeviceTransportConfig,
): DeviceCommandClient {
  return new DeviceCommandClient(
    new MockDeviceTransport(),
    null,
    undefined,
    config.startupTimeoutMs,
  );
}
