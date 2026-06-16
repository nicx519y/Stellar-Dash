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

function hexByte(value: number) {
  return `0x${value.toString(16).toUpperCase().padStart(2, "0")}`;
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

function displayInputMeta(packet: PacketEvent) {
  if (typeof rfInputKeyMask(packet) !== "number" && !packet.messageType.startsWith("RFH_RHI1_")) {
    return "-";
  }
  const bytes = payloadBytes(packet.payloadHex);
  const inputSeq = typeof packet.inputSeq === "number" ? packet.inputSeq : bytes[12];
  const inputFlags = typeof packet.inputFlags === "number" ? packet.inputFlags : bytes[13];
  if (typeof inputSeq !== "number" || typeof inputFlags !== "number") {
    return "-";
  }
  return `seq=${hexByte(inputSeq)} flags=${hexByte(inputFlags)}`;
}

function displayAirMeta(packet: PacketEvent) {
  if (!packet.messageType.startsWith("RFH_RHI1_")) {
    return "-";
  }

  const bytes = payloadBytes(packet.payloadHex);
  const rateHz = typeof packet.rateHz === "number" ? packet.rateHz : bytes.length >= 18 ? (bytes[16] | (bytes[17] << 8)) : undefined;
  const rateCode = typeof packet.airRateCode === "number" ? packet.airRateCode : bytes[18];
  const lastSeq = typeof packet.airLastDataSeq === "number" ? packet.airLastDataSeq : bytes[20];

  if (typeof rateHz !== "number" || typeof rateCode !== "number") {
    return "-";
  }

  const seqText = typeof lastSeq === "number" ? ` seq=${hexByte(lastSeq)}` : "";
  return `${rateHz}Hz code=${rateCode}${seqText}`;
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
    { key: "time", header: "Time", width: "145px", render: (p) => fmtTime(p.timestampMs) },
    { key: "type", header: "Type", width: "140px", render: (p) => displayMessageType(p.messageType) },
    { key: "cmd", header: "Cmd", width: "135px", render: (p) => displayCommand(p) },
    { key: "keyMask", header: "KeyMask", width: "120px", render: (p) => displayKeyMask(p) },
    { key: "buttons", header: "Buttons", width: "minmax(120px, 1fr)", render: (p) => displayButtons(p) },
    { key: "inputMeta", header: "Input", width: "145px", render: (p) => displayInputMeta(p) },
    { key: "air", header: "Air", width: "170px", render: (p) => displayAirMeta(p) },
    { key: "seq", header: "Seq", width: "90px", align: "end", render: (p) => (typeof p.seq === "number" ? p.seq : "-") },
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
