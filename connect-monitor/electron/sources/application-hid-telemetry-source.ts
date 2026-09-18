import type { MonitorEvent } from "../pipeline/types";

const MON1_MAGIC = 0x4d4f4e31;
const MONP_MAGIC = 0x4d4f4e50;
const MPW2_MAGIC = 0x3257504d;

function chargeStateFromCode(code: number): "Discharging" | "Charging" | "Full" | "Fault" | "Unknown" {
  switch (code) {
    case 0:
      return "Discharging";
    case 1:
      return "Charging";
    case 2:
      return "Full";
    case 3:
      return "Fault";
    default:
      return "Unknown";
  }
}

function appTelemetryOffset(report: Uint8Array): number | null {
  if (report.length >= 32) {
    const direct = new DataView(report.buffer, report.byteOffset, report.byteLength).getUint32(0, true);
    if (direct === MON1_MAGIC || direct === MONP_MAGIC || direct === MPW2_MAGIC) return 0;
  }
  if (report.length >= 33) {
    const shifted = new DataView(report.buffer, report.byteOffset, report.byteLength).getUint32(1, true);
    if (shifted === MON1_MAGIC || shifted === MONP_MAGIC || shifted === MPW2_MAGIC) return 1;
  }
  return null;
}

function ch585RoleFromCode(code: number): "Unknown" | "RF" | "USB" | "Maintenance" {
  switch (code) {
    case 1:
      return "RF";
    case 2:
      return "USB";
    case 3:
      return "Maintenance";
    default:
      return "Unknown";
  }
}

/**
 * Parse the application HID telemetry frames in XInput mode (MON1/MONP, 32 bytes).
 * Frame layouts follow MonitorTelemetryFrameV1 and MonitorPowerFrameV1 in application/Cpp_Core/Inc/monitor_telemetry.hpp.
 */
export function parseApplicationHidTelemetryFrame(report: Uint8Array, timestampMs = Date.now()): MonitorEvent[] {
  const offset = appTelemetryOffset(report);
  if (offset === null) {
    return [];
  }

  const view = new DataView(report.buffer, report.byteOffset + offset, report.byteLength - offset);
  const magic = view.getUint32(0, true);
  if (magic === MPW2_MAGIC) {
    const seq = view.getUint32(4, true);
    const cellMv = view.getUint16(12, true);
    const socPermille = view.getUint16(14, true);
    const vbusMv = view.getUint16(16, true);
    const chargeCurrentMa = view.getUint16(18, true);
    const faultBits = view.getUint16(20, true);
    const chargeState = chargeStateFromCode(view.getUint8(22));
    const ch585Role = ch585RoleFromCode(view.getUint8(23));
    const version = `${view.getUint8(24)}.${view.getUint8(25)}.${view.getUint8(26)}`;
    const flags = view.getUint8(27);

    return [
      {
        kind: "power_status",
        timestampMs,
        h1Mv: cellMv,
        h2Mv: 0,
        batMv: cellMv,
        cellMv,
        socPercent: Math.max(0, Math.min(100, socPermille / 10)),
        activeBattery: "H1",
        chargeState,
        valid: (flags & 0x01) !== 0,
        lowBattery: (flags & 0x02) !== 0,
        fastCharging: (flags & 0x04) !== 0,
        vbusPresent: (flags & 0x08) !== 0,
        gaugeOnline: (flags & 0x10) !== 0,
        chargerOnline: (flags & 0x20) !== 0,
        vbusMv,
        chargeCurrentMa,
        faultBits,
        ch585Role,
        ch585Version: version,
        formatVersion: 2,
      },
      {
        kind: "packet",
        timestampMs,
        channel: "USB",
        direction: "TX",
        seq,
        messageType: "APP_MPW2",
        payloadLen: report.length,
      },
    ];
  }

  if (magic === MONP_MAGIC) {
    const seq = view.getUint32(4, true);
    const h1Mv = view.getUint16(12, true);
    const h2Mv = view.getUint16(14, true);
    const batMv = view.getUint16(16, true);
    const socPermille = view.getUint16(18, true);
    const activeBattery = view.getUint8(20) === 1 ? "H2" : "H1";
    const chargeState = chargeStateFromCode(view.getUint8(21));
    const flags = view.getUint8(22);

    return [
      {
        kind: "power_status",
        timestampMs,
        h1Mv,
        h2Mv,
        batMv,
        socPercent: Math.max(0, Math.min(100, socPermille / 10)),
        activeBattery,
        chargeState,
        valid: (flags & 0x01) !== 0,
        lowBattery: (flags & 0x02) !== 0,
        cellMv: batMv || (activeBattery === "H2" ? h2Mv : h1Mv),
        fastCharging: (flags & 0x04) !== 0,
        faultBits: chargeState === "Fault" ? 1 : 0,
        formatVersion: 1,
      },
      {
        kind: "packet",
        timestampMs,
        channel: "USB",
        direction: "TX",
        seq,
        messageType: "APP_MONP",
        payloadLen: report.length,
      },
    ];
  }

  if (magic !== MON1_MAGIC) {
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
