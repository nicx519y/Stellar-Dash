import { Button } from "@chakra-ui/react";
import { GrDocumentDownload } from "react-icons/gr";

import type { PacketEvent } from "../../../shared/monitor-types";
import { buildPacketLogMarkdown, exportMarkdown } from "./logExport";
import { toolbarActionButtonProps } from "./panelStyles";
import { VirtualTable, type VirtualColumn } from "./VirtualTable";

function fmtTime(ms: number) {
  const d = new Date(ms);
  return d.toLocaleTimeString() + "." + String(d.getMilliseconds()).padStart(3, "0");
}

function displayMessageType(messageType: string) {
  if (messageType.startsWith("RFH_RHM1_")) {
    return `HID_TELE_${messageType.slice("RFH_RHM1_".length)}`;
  }
  if (messageType.startsWith("RFH_RHI1_")) {
    return `HID_INPUT_${messageType.slice("RFH_RHI1_".length)}`;
  }
  if (messageType.startsWith("RFH_R5_")) {
    return `SERIAL_R5_${messageType.slice("RFH_R5_".length)}`;
  }
  return messageType;
}

function payloadBytes(payloadHex: string | undefined) {
  if (!payloadHex) return [];
  return payloadHex
    .trim()
    .split(/\s+/)
    .map((part) => Number.parseInt(part, 16))
    .filter((byte) => Number.isFinite(byte) && byte >= 0 && byte <= 0xff);
}

function hexU32(value: number) {
  return `0x${(value >>> 0).toString(16).toUpperCase().padStart(8, "0")}`;
}

function rfInputKeyMask(packet: PacketEvent) {
  if (typeof packet.inputKeyMask === "number") {
    return packet.inputKeyMask >>> 0;
  }
  const bytes = payloadBytes(packet.payloadHex);
  if (bytes.length >= 12 && bytes[0] === 0x52 && bytes[1] === 0x48 && bytes[2] === 0x49 && bytes[3] === 0x31) {
    return ((bytes[8] ?? 0) |
      ((bytes[9] ?? 0) << 8) |
      ((bytes[10] ?? 0) << 16) |
      ((bytes[11] ?? 0) << 24)) >>> 0;
  }
  return undefined;
}

function displayCommand(packet: PacketEvent) {
  if (typeof rfInputKeyMask(packet) === "number" || packet.messageType.startsWith("RFH_RHI1_")) {
    return "0x06 INPUT_DATA";
  }
  if (packet.messageType.startsWith("RFH_RHM1_")) return "RHM1 TELE";
  if (packet.messageType === "RFH_RHS1_SCORE") return "RHS1 SCORE";
  if (packet.messageType === "DONGLE_DMN1") return "DMN1";
  return "-";
}

const hboxButtons = [
  ["UP", 0],
  ["DOWN", 1],
  ["LEFT", 2],
  ["RIGHT", 3],
  ["B1", 4],
  ["B2", 5],
  ["B3", 6],
  ["B4", 7],
  ["L1", 8],
  ["R1", 9],
  ["L2", 10],
  ["R2", 11],
  ["S1", 12],
  ["S2", 13],
  ["L3", 14],
  ["R3", 15],
  ["A1", 16],
  ["A2", 17],
] as const;

function displayKeyMask(packet: PacketEvent) {
  const mask = rfInputKeyMask(packet);
  return typeof mask === "number" ? hexU32(mask) : "-";
}

function displayButtons(packet: PacketEvent) {
  const mask = rfInputKeyMask(packet);
  if (typeof mask !== "number") return "-";
  const pressed = hboxButtons.filter(([, bit]) => (mask & (1 << bit)) !== 0).map(([label]) => label);
  return pressed.length > 0 ? pressed.join(" ") : "none";
}

function displayAirMeta(packet: PacketEvent) {
  if (!packet.messageType.startsWith("RFH_RHI1_")) {
    return "-";
  }

  const bytes = payloadBytes(packet.payloadHex);
  const rawPendingDrop = bytes.length >= 23 ? (bytes[21] ?? 0) | ((bytes[22] ?? 0) << 8) : undefined;
  const rawPendingCurrent = bytes.length >= 24 ? bytes[23] : undefined;
  const rawPendingMax = bytes.length >= 25 ? bytes[24] : undefined;
  const rawWindowRx = bytes.length >= 27 ? (bytes[25] ?? 0) | ((bytes[26] ?? 0) << 8) : undefined;
  const rawWindowExpected = bytes.length >= 29 ? (bytes[27] ?? 0) | ((bytes[28] ?? 0) << 8) : undefined;
  const rawCrcErrors = bytes.length >= 31 ? (bytes[29] ?? 0) | ((bytes[30] ?? 0) << 8) : undefined;
  const rawTypeErrors = bytes.length >= 32 ? (bytes[31] ?? 0) >> 4 : undefined;
  const rawTimeoutErrors = bytes.length >= 32 ? (bytes[31] ?? 0) & 0x0f : undefined;
  const eventPendingCurrent = packet.airPendingCurrent;
  const eventPendingMax = packet.airPendingMax;
  const eventLooksSane =
    typeof packet.airPendingDrop === "number" &&
    typeof eventPendingCurrent === "number" &&
    typeof eventPendingMax === "number" &&
    eventPendingMax <= 16 &&
    eventPendingCurrent <= eventPendingMax;

  const pendingDrop = eventLooksSane ? packet.airPendingDrop : rawPendingDrop;
  const pendingCurrent = eventLooksSane ? eventPendingCurrent : rawPendingCurrent;
  const pendingMax = eventLooksSane ? eventPendingMax : rawPendingMax;
  const windowRx = eventLooksSane && typeof packet.airWindowRxOk === "number" ? packet.airWindowRxOk : rawWindowRx;
  const windowExpected =
    eventLooksSane && typeof packet.airWindowExpected === "number" ? packet.airWindowExpected : rawWindowExpected;
  const crcErrors =
    eventLooksSane && typeof packet.airWindowCrcErrors === "number" ? packet.airWindowCrcErrors : rawCrcErrors;
  const typeErrors =
    eventLooksSane && typeof packet.airWindowTypeErrors === "number" ? packet.airWindowTypeErrors : rawTypeErrors;
  const timeoutErrors =
    eventLooksSane && typeof packet.airWindowTimeoutErrors === "number" ? packet.airWindowTimeoutErrors : rawTimeoutErrors;

  if (typeof pendingDrop !== "number" || typeof pendingCurrent !== "number" || typeof pendingMax !== "number") {
    return "-";
  }

  const lossText =
    typeof windowRx === "number" && typeof windowExpected === "number" && windowExpected > 0
      ? ` win=${windowRx}/${windowExpected} loss=${(((windowExpected - Math.min(windowRx, windowExpected)) * 100) / windowExpected).toFixed(1)}%`
      : "";
  const missingText =
    typeof windowRx === "number" && typeof windowExpected === "number" && windowExpected > 0
      ? ` miss=${Math.max(0, windowExpected - Math.min(windowRx, windowExpected) - (crcErrors ?? 0))}`
      : "";
  const errorText =
    typeof crcErrors === "number" || typeof typeErrors === "number" || typeof timeoutErrors === "number"
      ? ` crc=${crcErrors ?? 0} type=${typeErrors ?? 0} to=${timeoutErrors ?? 0}`
      : "";
  return `pend=${pendingCurrent}/${pendingMax} drop=${pendingDrop}${lossText}${missingText}${errorText}`;
}

export function PacketsPanel({ items, fillHeight = false }: { items: Array<PacketEvent & { id?: string }>; fillHeight?: boolean }) {
  const rows = items
    .filter((packet) => packet.messageType.startsWith("RFH_RHI1_") && packet.rfStateCode === "C")
    .slice(-500)
    .reverse();
  const handleExport = () => {
    void exportMarkdown("packet-log.md", buildPacketLogMarkdown(items));
  };
  const columns: Array<VirtualColumn<PacketEvent & { id?: string }>> = [
    { key: "time", header: "Time", width: "11%", render: (p) => fmtTime(p.timestampMs) },
    { key: "type", header: "Type", width: "12%", render: (p) => displayMessageType(p.messageType) },
    { key: "cmd", header: "Cmd", width: "11%", render: (p) => displayCommand(p) },
    { key: "keyMask", header: "KeyMask", width: "11%", render: (p) => displayKeyMask(p) },
    { key: "buttons", header: "Buttons", width: "8%", render: (p) => displayButtons(p) },
    { key: "air", header: "Air", width: "42%", render: (p) => displayAirMeta(p) },
    { key: "seq", header: "Seq", width: "5%", align: "end", render: (p) => (typeof p.seq === "number" ? p.seq : "-") },
  ];

  return (
    <VirtualTable
      title="Packets"
      countText={`Recent ${rows.length} / 500`}
      action={
        <Button {...toolbarActionButtonProps} onClick={handleExport}>
          <GrDocumentDownload />
          Export
        </Button>
      }
      items={rows}
      columns={columns}
      rowKey={(p, idx) => p.id ?? `${p.timestampMs}-${idx}`}
      maxHeight={fillHeight ? "100%" : 530}
    />
  );
}
