import { DeviceTransportError } from './types';

export interface ServerWebConfigTarget {
  productId: string;
  pcbRevision: string;
  webConfigProfile: string;
}

export interface ResolvedWebConfigTarget extends ServerWebConfigTarget {
  basePath: string;
}

type SupportedTarget = Readonly<{
  productId: string;
  pcbRevision: string;
  webConfigProfile: string;
}>;

/**
 * Browser-side component/profile registry. The server makes the trust
 * decision; this second allowlist prevents a compromised or misconfigured
 * response from selecting an unknown bundle or navigation path.
 */
export const SUPPORTED_WEB_CONFIG_TARGETS: readonly SupportedTarget[] = [
  {
    productId: 'HBOX',
    pcbRevision: '2.0.0',
    webConfigProfile: 'hbox-pcb-v2',
  },
] as const;

export function resolveAuthenticatedWebConfigTarget(
  value: ServerWebConfigTarget,
  attestedHardwareVersion: string,
): ResolvedWebConfigTarget {
  if (
    !value ||
    typeof value.productId !== 'string' ||
    typeof value.pcbRevision !== 'string' ||
    typeof value.webConfigProfile !== 'string' ||
    value.pcbRevision !== attestedHardwareVersion
  ) {
    throw new DeviceTransportError(
      'authentication-failed',
      '认证服务器返回的产品或 PCB 信息与设备证明不一致',
    );
  }

  const supported = SUPPORTED_WEB_CONFIG_TARGETS.find((target) =>
    target.productId === value.productId &&
    target.pcbRevision === value.pcbRevision &&
    target.webConfigProfile === value.webConfigProfile
  );
  if (!supported) {
    throw new DeviceTransportError(
      'authentication-failed',
      `此 WebConfig 不支持产品 ${value.productId} 的 PCB ${value.pcbRevision}`,
    );
  }

  return {
    ...supported,
    // The path is derived only from a compile-time profile slug. Never use a
    // server-provided URL, which could navigate the bearer-token page to a
    // different origin.
    basePath: `/webconfig/${supported.webConfigProfile}/`,
  };
}
