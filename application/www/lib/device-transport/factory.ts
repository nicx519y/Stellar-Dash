import { DeviceAuthClient } from './device-auth-client';
import { DeviceTransportFrameworkAdapter } from './framework-adapter';
import { LegacyWebSocketTransport } from './legacy-websocket-transport';
import { MockDeviceTransport } from './mock-device-transport';
import { WebHidTransport } from './webhid-transport';

export type DeviceTransportMode = 'webhid' | 'legacy-websocket' | 'mock';

export interface DeviceTransportFactoryOptions {
  mode: DeviceTransportMode;
  websocket: {
    url: string;
    heartbeatInterval: number;
    timeout: number;
  };
}

export function createDeviceTransportFramework(
  options: DeviceTransportFactoryOptions,
): DeviceTransportFrameworkAdapter {
  if (options.mode === 'mock') {
    return new DeviceTransportFrameworkAdapter(new MockDeviceTransport());
  }

  if (options.mode === 'legacy-websocket') {
    return new DeviceTransportFrameworkAdapter(
      new LegacyWebSocketTransport(options.websocket),
    );
  }

  const filters = [{
    usagePage: parseHexOrDecimal(process.env.NEXT_PUBLIC_WEBHID_USAGE_PAGE, 0xff00),
    usage: parseHexOrDecimal(process.env.NEXT_PUBLIC_WEBHID_USAGE, 0x01),
    vendorId: parseHexOrDecimal(process.env.NEXT_PUBLIC_WEBHID_VENDOR_ID, 0xcafe),
    productId: parseHexOrDecimal(process.env.NEXT_PUBLIC_WEBHID_PRODUCT_ID, 0x4021),
  }];
  const transport = new WebHidTransport({
    filters,
    reportId: parseHexOrDecimal(process.env.NEXT_PUBLIC_WEBHID_REPORT_ID, 0),
    requestTimeoutMs: options.websocket.timeout,
  });
  const auth = new DeviceAuthClient({
    // Hosted V2 deliberately uses the page origin for both authentication
    // and protected downloads.  Cross-origin values from .env.local are not
    // accepted in a deployable WebHID build.
    challengeEndpoint: '/api/v2/device-auth/challenges',
    verifyEndpoint: '/api/v2/device-auth/verify',
    scopes: [
      'config.read',
      'config.write',
      'monitor.read',
    ],
  });
  return new DeviceTransportFrameworkAdapter(transport, auth);
}

export function configuredTransportMode(): DeviceTransportMode {
  if (
    process.env.NEXT_PUBLIC_DEVICE_TRANSPORT === 'mock' &&
    process.env.NEXT_PUBLIC_OFFLINE_PREVIEW === 'true'
  ) {
    return 'mock';
  }
  return process.env.NEXT_PUBLIC_DEVICE_TRANSPORT === 'legacy-websocket'
    ? 'legacy-websocket'
    : 'webhid';
}

function parseHexOrDecimal(value: string | undefined, fallback: number): number {
  if (!value) return fallback;
  const parsed = Number(value);
  return Number.isInteger(parsed) && parsed >= 0 ? parsed : fallback;
}
