import type { DeviceTransportMode } from './factory';

/**
 * Hosted V2 keeps device-auth, catalog and protected downloads on one origin.
 */
export function resolveDefaultFirmwareServerHost(
  _transportMode: DeviceTransportMode,
  _configuredHost?: string,
): string {
  // Authenticated downloads must remain on the page origin. In particular,
  // do not let an ignored .env.local bake a remote bearer-token destination
  // into a production export.
  return '';
}
