export interface HidDeviceInfo {
  vendorId?: number;
  productId?: number;
  usagePage?: number;
  usage?: number;
  interface?: number;
  interfaceNumber?: number;
  manufacturer?: string;
  product?: string;
  serialNumber?: string;
}

export interface HidTarget {
  vendorId: number | null;
  productId: number | null;
}

const DEFAULT_TELEMETRY_USB_IDS = [
  { vendorId: 0x045e, productId: 0x028e },
  { vendorId: 0x045e, productId: 0x02ff },
  { vendorId: 0x1a86, productId: 0xfe0c },
] as const;

const WEB_CONFIG_USB_ID = { vendorId: 0xcafe, productId: 0x4021 } as const;

function textIncludes(value: unknown, needle: string): boolean {
  return typeof value === "string" && value.toLowerCase().includes(needle);
}

function isLikelyHBoxDevice(device: HidDeviceInfo): boolean {
  return textIncludes(device.manufacturer, "hbox") || textIncludes(device.product, "hbox");
}

function isWebConfigInterface(device: HidDeviceInfo): boolean {
  return (
    (device.vendorId === WEB_CONFIG_USB_ID.vendorId && device.productId === WEB_CONFIG_USB_ID.productId) ||
    textIncludes(device.product, "webconfig") ||
    textIncludes(device.serialNumber, "hbox-webconfig")
  );
}

function isKnownTelemetryUsbId(device: HidDeviceInfo): boolean {
  return DEFAULT_TELEMETRY_USB_IDS.some(
    (id) => device.vendorId === id.vendorId && device.productId === id.productId,
  );
}

function isLikelyTelemetryInterface(device: HidDeviceInfo): boolean {
  const interfaceNumber =
    typeof device.interface === "number"
      ? device.interface
      : typeof device.interfaceNumber === "number"
        ? device.interfaceNumber
        : undefined;

  if (device.usagePage === 0xff00) return true;
  if (interfaceNumber === 3 && isLikelyHBoxDevice(device)) return true;
  return false;
}

/**
 * Select only the HID interface carrying MON1/DMN1/RHM1 telemetry.
 *
 * WebConfig deliberately uses the same vendor usage page, so usagePage alone
 * is not an identity. The normal path is restricted to known telemetry USB
 * IDs. Explicit VID/PID overrides may support bring-up hardware, but still
 * must look like a telemetry interface and can never select WebConfig.
 */
export function matchesHidTelemetryDevice(
  device: HidDeviceInfo,
  target: HidTarget,
): boolean {
  if (isWebConfigInterface(device)) return false;
  if (!isLikelyTelemetryInterface(device)) return false;

  const hasExplicitTarget = target.vendorId !== null || target.productId !== null;
  if (hasExplicitTarget) {
    if (target.vendorId !== null && device.vendorId !== target.vendorId) return false;
    if (target.productId !== null && device.productId !== target.productId) return false;
    return true;
  }

  return isKnownTelemetryUsbId(device);
}
