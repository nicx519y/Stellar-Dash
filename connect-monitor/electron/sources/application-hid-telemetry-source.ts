import type { MonitorEvent } from "../pipeline/types";

/**
 * 解析 application 在 XInput 模式下的 HID telemetry 帧（MON1, 32 bytes）。
 * 帧布局参考 application/Cpp_Core/Src/drivers/xinput/XInputDriver.cpp 的 XInputTelemetryFrame。
 */
export function parseApplicationHidTelemetryFrame(report: Uint8Array, timestampMs = Date.now()): MonitorEvent[] {
  if (report.length < 32) {
    return [];
  }

  const view = new DataView(report.buffer, report.byteOffset, report.byteLength);
  const magic = view.getUint32(0, true);
  if (magic !== 0x4d4f4e31) {
    return [];
  }

  const totalReports = view.getUint32(8, true);
  const usbReportsCompleted = view.getUint32(12, true);
  const targetRateHz = view.getUint16(16, true);
  const latestUsbLatencyUs = view.getUint16(20, true);
  const avgUsbLatencyUs = view.getUint16(22, true);
  const latestRfLatencyUs = view.getUint16(24, true);
  const avgRfLatencyUs = view.getUint16(26, true);
  const rfTransferOk = view.getUint16(28, true);
  const rfTransferFail = view.getUint16(30, true);

  return [
    {
      kind: "device_status",
      timestampMs,
      mode: "USB",
      state: "Connected",
      targetRateHz,
      actualRateHz: targetRateHz,
    },
    {
      kind: "latency",
      timestampMs,
      seq: totalReports,
      deviceToUsbSubmitUs: latestUsbLatencyUs || undefined,
      deviceToRfUs: latestRfLatencyUs || undefined,
    },
    {
      kind: "packet",
      timestampMs,
      channel: "USB",
      direction: "TX",
      seq: totalReports,
      messageType: "APP_MON1",
      payloadLen: report.length,
    },
    {
      kind: "error",
      timestampMs,
      source: "APP_MONITOR",
      code: "STATS",
      level: "INFO",
      message: `usbDone=${usbReportsCompleted} avgUsbUs=${avgUsbLatencyUs} avgRfUs=${avgRfLatencyUs} rfOk=${rfTransferOk} rfFail=${rfTransferFail}`,
    },
  ];
}
