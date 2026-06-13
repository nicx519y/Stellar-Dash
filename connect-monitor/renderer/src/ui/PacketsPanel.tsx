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
  if (messageType.startsWith("RFH_R5_")) {
    return `SERIAL_R5_${messageType.slice("RFH_R5_".length)}`;
  }
  return messageType;
}

export function PacketsPanel({ items }: { items: Array<PacketEvent & { id?: string }> }) {
  const rows = items.slice(-500).reverse();
  const handleExport = () => {
    void exportMarkdown("packet-log.md", buildPacketLogMarkdown(items));
  };
  const columns: Array<VirtualColumn<PacketEvent & { id?: string }>> = [
    { key: "time", header: "Time", width: "130px", render: (p) => fmtTime(p.timestampMs) },
    { key: "type", header: "Type", width: "150px", render: (p) => displayMessageType(p.messageType) },
    { key: "len", header: "Length", width: "70px", align: "end", render: (p) => p.payloadLen },
    { key: "seq", header: "Seq", width: "100px", align: "end", render: (p) => (typeof p.seq === "number" ? p.seq : "-") },
    {
      key: "stats",
      header: "Stats",
      width: "150px",
      align: "end",
      render: (p) =>
        typeof p.sampleCount === "number"
          ? `${p.sampleCount}/${p.expectedCount ?? "-"}`
          : typeof p.rateHz === "number"
            ? `${p.rateHz.toFixed(1)}Hz`
            : "-",
    },
    {
      key: "loss",
      header: "Loss",
      width: "90px",
      align: "end",
      render: (p) => (typeof p.lossPermille === "number" ? `${(p.lossPermille / 10).toFixed(2)}%` : "-"),
    },
    {
      key: "rate",
      header: "Rate",
      width: "100px",
      align: "end",
      render: (p) => (typeof p.rateHz === "number" ? `${p.rateHz.toFixed(1)}Hz` : "-"),
    },
    {
      key: "channelNumber",
      header: "Channel",
      width: "70px",
      align: "end",
      render: (p) => (typeof p.channelNumber === "number" ? p.channelNumber : "-"),
    },
    {
      key: "target",
      header: "Target",
      width: "70px",
      align: "end",
      render: (p) => (typeof p.targetChannelNumber === "number" ? p.targetChannelNumber : "-"),
    },
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
    />
  );
}
