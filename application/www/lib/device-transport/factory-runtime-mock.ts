import { DeviceCommandClient } from './device-command-client';
import { DeviceTransportConfig } from './device-command-types';
import { MockDeviceTransport } from './mock-device-transport';
import { DeviceTransportError } from './types';

export const BUILD_DEVICE_TRANSPORT_MODE = 'mock' as const;
export { getReceiverHid } from './mock-rf-binding';

export function createBuildDeviceCommandClient(
  config: DeviceTransportConfig,
): DeviceCommandClient {
  // Reproducible browser QA controls, confined to the mock build alias.
  const query = new URLSearchParams(typeof window === 'undefined' ? '' : window.location.search);
  const delay = Math.min(30_000, Math.max(0, Number(query.get('mockConfigWriteDelayMs')) || 0));
  let failures = Math.min(10, Math.max(0, Number(query.get('mockConfigWriteFailures')) || 0));
  let disconnectOnce = query.get('mockConfigDisconnectOnce') === '1';
  const transport: MockDeviceTransport = new MockDeviceTransport({ beforeRequest: async command => {
    if (!['update_global_config', 'update_screen_control_config', 'update_hotkeys_config', 'update_profile', 'update_profile_macros', 'switch_default_profile'].includes(command)) return;
    if (delay) await new Promise(resolve => setTimeout(resolve, delay));
    if (disconnectOnce) {
      disconnectOnce = false;
      await transport.close();
      throw new DeviceTransportError('disconnected', 'Mock device disconnected during save');
    }
    if (failures > 0) { failures--; throw new DeviceTransportError('protocol', 'Mock configuration write failed'); }
  } });
  return new DeviceCommandClient(transport, null, undefined, config.startupTimeoutMs);
}
