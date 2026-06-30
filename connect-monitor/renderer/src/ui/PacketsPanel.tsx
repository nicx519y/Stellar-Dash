import { Button, HStack } from "@chakra-ui/react";
import { GrDocumentDownload } from "react-icons/gr";

import type { PacketEvent } from "../../../shared/monitor-types";
import { buildPacketLogMarkdown, exportMarkdown } from "./logExport";
import { ClearDataIconButton } from "./panelActions";
import { toolbarActionButtonProps } from "./panelStyles";
import { VirtualTable, type VirtualColumn } from "./VirtualTable";

function fmtTime(ms: number) {
  const d = new Date(ms);
  return d.toLocaleTimeString() + "." + String(d.getMilliseconds()).padStart(3, "0");
}

function displayMessageType(messageType: string) {
  if (messageType.startsWith("RFH_RHM1_")) {
    return `RHM1 ${messageType.slice("RFH_RHM1_".length)}`;
  }
  if (messageType.startsWith("RFH_R5_")) {
    return `SERIAL_R5_${messageType.slice("RFH_R5_".length)}`;
  }
  if (messageType === "RFH_RHS1_SCORE") return "RHS1 SCORE";
  if (messageType.startsWith("RFH_RHR1_")) return `RHR1 ${messageType.slice("RFH_RHR1_".length)}`;
  return messageType;
}

function isQualityPacket(packet: PacketEvent) {
  return packet.messageType.startsWith("RFH_RHM1_") ||
    packet.messageType === "RFH_RHS1_SCORE" ||
    packet.messageType.startsWith("RFH_RHR1_");
}

function fmtHz(value: number | undefined) {
  if (typeof value !== "number" || !Number.isFinite(value)) return "-";
  return `${Math.round(value)}Hz`;
}

function fmtLoss(packet: PacketEvent) {
  if (typeof packet.lossPermille !== "number") return "-";
  return `${(packet.lossPermille / 10).toFixed(1)}%`;
}

function displayState(packet: PacketEvent) {
  return packet.rfStateCode ?? (packet.messageType === "RFH_RHS1_SCORE" ? "score" : "-");
}

function displayChannel(packet: PacketEvent) {
  if (packet.messageType === "RFH_RHS1_SCORE") {
    const active = typeof packet.channelNumber === "number" ? `active=${packet.channelNumber}` : "active=-";
    const score = typeof packet.activeChannelScore === "number" ? ` score=${packet.activeChannelScore}` : "";
    return `${active}${score}`;
  }
  const current = typeof packet.channelNumber === "number" ? packet.channelNumber : "-";
  const target = typeof packet.targetChannelNumber === "number" ? packet.targetChannelNumber : "-";
  const old = typeof packet.oldChannelNumber === "number" ? packet.oldChannelNumber : "-";
  return `${current} -> ${target} old=${old}`;
}

function displayRate(packet: PacketEvent) {
  const actual = fmtHz(packet.rateHz);
  const target = fmtHz(packet.targetRateHz);
  if (actual === "-" && target === "-") return "-";
  return `${actual} / ${target}`;
}

function displayRssi(packet: PacketEvent) {
  if (typeof packet.rssiLast !== "number") return "-";
  const avg = typeof packet.rssiAvg === "number" ? packet.rssiAvg : "-";
  const min = typeof packet.rssiMin === "number" ? packet.rssiMin : "-";
  const max = typeof packet.rssiMax === "number" ? packet.rssiMax : "-";
  const samples = typeof packet.rssiSamples === "number" ? ` n=${packet.rssiSamples}` : "";
  return `${packet.rssiLast} avg=${avg} min=${min} max=${max}${samples}`;
}

function displayWindow(packet: PacketEvent) {
  const rx = typeof packet.sampleCount === "number" ? packet.sampleCount : "-";
  const expected = typeof packet.expectedCount === "number" ? packet.expectedCount : "-";
  const elapsed = typeof packet.sampleWindowMs === "number" ? `${packet.sampleWindowMs}ms` : "-";
  return `${rx}/${expected} ${elapsed}`;
}

function displayEvents(packet: PacketEvent) {
  const parts: string[] = [];
  if (typeof packet.errorEvents === "number") parts.push(`err=${packet.errorEvents}`);
  if (typeof packet.maxSilentMs === "number") parts.push(`silent=${packet.maxSilentMs}ms`);
  if (packet.hopEvent === "start") parts.push(`hop=start score=${packet.hopScorePermille ?? packet.hopEventValue ?? "-"}`);
  if (packet.hopEvent === "finish") parts.push(`hop=finish ${packet.hopDurationMs ?? packet.hopEventValue ?? "-"}ms`);
  return parts.length > 0 ? parts.join(" ") : "-";
}

function displayScores(packet: PacketEvent) {
  if (!packet.channelScores || packet.channelScores.length === 0) return "-";
  return packet.channelScores
    .map((entry) => `${entry.channel}${entry.channel === packet.channelNumber ? "*" : ""}:${entry.score}`)
    .join(" ");
}

export function PacketsPanel({
  items,
  fillHeight = false,
  onClearData,
}: {
  items: Array<PacketEvent & { id?: string }>;
  fillHeight?: boolean;
  onClearData?: () => void;
}) {
  const rows = items
    .filter(isQualityPacket)
    .slice(-500)
    .reverse();
  const handleExport = () => {
    void exportMarkdown("packet-log.md", buildPacketLogMarkdown(items));
  };
  const columns: Array<VirtualColumn<PacketEvent & { id?: string }>> = [
    { key: "time", header: "Time", width: "9%", render: (p) => fmtTime(p.timestampMs) },
    { key: "type", header: "Type", width: "8%", render: (p) => displayMessageType(p.messageType) },
    { key: "state", header: "State", width: "5%", render: (p) => displayState(p) },
    { key: "channel", header: "Channel", width: "11%", render: (p) => displayChannel(p) },
    { key: "rate", header: "Rate", width: "10%", render: (p) => displayRate(p) },
    { key: "loss", header: "Loss", width: "6%", align: "end", render: (p) => fmtLoss(p) },
    { key: "rssi", header: "RSSI", width: "13%", render: (p) => displayRssi(p) },
    { key: "window", header: "RX/Expected", width: "11%", render: (p) => displayWindow(p) },
    { key: "events", header: "Events", width: "12%", render: (p) => displayEvents(p) },
    { key: "scores", header: "Scores", width: "10%", render: (p) => displayScores(p) },
    { key: "seq", header: "Seq", width: "5%", align: "end", render: (p) => (typeof p.seq === "number" ? p.seq : "-") },
  ];

  return (
    <VirtualTable
      title="RF Quality Packets"
      countText={`Recent quality ${rows.length} / 500`}
      action={
        <HStack gap={2}>
          <Button {...toolbarActionButtonProps} onClick={handleExport}>
            <GrDocumentDownload />
            Export
          </Button>
          {onClearData ? <ClearDataIconButton label="Clear packet data" onClick={onClearData} /> : null}
        </HStack>
      }
      items={rows}
      columns={columns}
      rowKey={(p, idx) => p.id ?? `${p.timestampMs}-${idx}`}
      maxHeight={fillHeight ? "100%" : 530}
    />
  );
}
