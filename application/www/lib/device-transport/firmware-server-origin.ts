import { DEFAULT_FIRMWARE_SERVER_HOST } from '../../types/gamepad-config';
import type { DeviceTransportMode } from './factory';

/**
 * Hosted V2 keeps device-auth, catalog and protected downloads on one origin.
 * The remote default is retained only for the explicit legacy WebSocket build.
 */
export function resolveDefaultFirmwareServerHost(
  transportMode: DeviceTransportMode,
  configuredHost?: string,
): string {
  // Authenticated V2 downloads must remain on the page origin.  In
  // particular, do not let a developer's ignored .env.local silently bake a
  // remote bearer-token destination into a hosted production export.
  if (transportMode !== 'legacy-websocket') {
    return '';
  }
  const explicitHost = configuredHost?.trim();
  if (explicitHost) {
    return explicitHost.replace(/\/+$/, '');
  }
  return DEFAULT_FIRMWARE_SERVER_HOST;
}
