/*
 * WebHID is not part of TypeScript's lib.dom declarations. Keep the minimal
 * surface used by WebConfig here instead of adding an ambient global shim.
 */

export interface WebHidDeviceFilter {
  vendorId?: number;
  productId?: number;
  usagePage?: number;
  usage?: number;
}

export interface WebHidDeviceRequestOptions {
  filters: WebHidDeviceFilter[];
  exclusionFilters?: WebHidDeviceFilter[];
}

export interface WebHidInputReportEvent extends Event {
  readonly device: WebHidDevice;
  readonly reportId: number;
  readonly data: DataView;
}

export interface WebHidDevice {
  readonly opened: boolean;
  readonly vendorId: number;
  readonly productId: number;
  readonly productName: string;
  readonly collections: readonly unknown[];
  open(): Promise<void>;
  close(): Promise<void>;
  sendReport(reportId: number, data: BufferSource): Promise<void>;
  addEventListener(
    type: 'inputreport',
    listener: (event: WebHidInputReportEvent) => void,
  ): void;
  removeEventListener(
    type: 'inputreport',
    listener: (event: WebHidInputReportEvent) => void,
  ): void;
}

export interface WebHidNavigator {
  getDevices(): Promise<WebHidDevice[]>;
  requestDevice(options: WebHidDeviceRequestOptions): Promise<WebHidDevice[]>;
  addEventListener(
    type: 'disconnect',
    listener: (event: Event & { device?: WebHidDevice }) => void,
  ): void;
  removeEventListener(
    type: 'disconnect',
    listener: (event: Event & { device?: WebHidDevice }) => void,
  ): void;
}

export function getWebHidNavigator(): WebHidNavigator | null {
  if (typeof navigator === 'undefined') {
    return null;
  }
  return (navigator as Navigator & { hid?: WebHidNavigator }).hid ?? null;
}
