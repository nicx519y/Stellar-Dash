import { DeviceAuthClient } from './device-auth-client';
import { DeviceCommandClient } from './device-command-client';
import { DeviceTransportConfig } from './device-command-types';
import { createBrowserWebHidDeviceLease } from './device-lease';
import { WebHidTransport } from './webhid-transport';

export const BUILD_DEVICE_TRANSPORT_MODE = 'webhid' as const;

export function createBuildDeviceCommandClient(
  config: DeviceTransportConfig,
): DeviceCommandClient {
  const filters = [{
    usagePage: parseHexOrDecimal(process.env.NEXT_PUBLIC_WEBHID_USAGE_PAGE, 0xff00),
    usage: parseHexOrDecimal(process.env.NEXT_PUBLIC_WEBHID_USAGE, 0x01),
    vendorId: parseHexOrDecimal(process.env.NEXT_PUBLIC_WEBHID_VENDOR_ID, 0xcafe),
    productId: parseHexOrDecimal(process.env.NEXT_PUBLIC_WEBHID_PRODUCT_ID, 0x4021),
  }];
  const connectionLease = createBrowserWebHidDeviceLease();
  const transport = new WebHidTransport({
    filters,
    reportId: parseHexOrDecimal(process.env.NEXT_PUBLIC_WEBHID_REPORT_ID, 0),
    requestTimeoutMs: config.requestTimeoutMs,
    openTimeoutMs: config.openTimeoutMs,
    closeTimeoutMs: config.closeTimeoutMs,
    connectionLease,
  });
  const initialScopes = [
    'config.read',
    'config.write',
    'monitor.read',
    // The Global page immediately checks calibration state and starts input
    // monitoring. Both are device-control RPCs, so include that scope in the
    // initial permit instead of replacing a just-opened secure HID session.
    'device.control',
  ] as const;
  const auth = new DeviceAuthClient({
    // Hosted V2 deliberately uses the page origin for both authentication
    // and protected downloads. Cross-origin values from .env.local are not
    // accepted in a deployable WebHID build.
    challengeEndpoint: '/api/v2/device-auth/challenges',
    verifyEndpoint: '/api/v2/device-auth/verify',
    scopes: initialScopes,
  });
  return new DeviceCommandClient(
    transport,
    auth,
    initialScopes,
    config.startupTimeoutMs,
  );
}

function parseHexOrDecimal(value: string | undefined, fallback: number): number {
  if (!value) return fallback;
  const parsed = Number(value);
  return Number.isInteger(parsed) && parsed >= 0 ? parsed : fallback;
}
