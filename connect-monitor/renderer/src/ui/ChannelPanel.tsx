import { Badge, Button } from "@chakra-ui/react";
import { GrDocumentDownload } from "react-icons/gr";

import { buildChannelEventLogMarkdown, exportMarkdown } from "./logExport";
import { toolbarActionButtonProps } from "./panelStyles";
import type { ChannelSwitchRow } from "./useMonitorStream";
import { VirtualTable, type VirtualColumn } from "./VirtualTable";

function fmtTime(ms: number) {
  const d = new Date(ms);
  return d.toLocaleTimeString() + "." + String(d.getMilliseconds()).padStart(3, "0");
}

function reasonColor(reason: string) {
  const text = reason.toLowerCase();
  if (text.includes("disconnect") || text.includes("reconnect") || text.includes("ack")) return "red";
  if (text.includes("loss") || text.includes("quality")) return "yellow";
  return "gray";
}

function typeColor(type: ChannelSwitchRow["type"]) {
  if (type === "hop_start") return "blue";
  if (type === "hop_finish") return "green";
  if (type === "link_lost") return "yellow";
  if (type === "link_recovered") return "green";
  if (type === "channel_change" || type === "target_change") return "cyan";
  return "gray";
}

function formatType(type: ChannelSwitchRow["type"]) {
  const map: Record<ChannelSwitchRow["type"], string> = {
    current: "Current",
    channel_change: "Channel Changed",
    hop_start: "Hop Started",
    hop_finish: "Hop Finished",
    link_lost: "Link Lost",
    link_recovered: "Link Recovered",
    target_change: "Target Changed",
  };
  return map[type];
}

export function ChannelPanel({ items, fillHeight = false }: { items: ChannelSwitchRow[]; fillHeight?: boolean }) {
  const rows = items.slice(-500).reverse();
  const handleExport = () => {
    void exportMarkdown("channel-event-log.md", buildChannelEventLogMarkdown(items));
  };
  const columns: Array<VirtualColumn<ChannelSwitchRow>> = [
    { key: "time", header: "Time", width: "16%", render: (row) => fmtTime(row.timestampMs) },
    {
      key: "type",
      header: "Type",
      width: "18%",
      render: (row) => <Badge colorPalette={typeColor(row.type)}>{formatType(row.type)}</Badge>,
    },
    { key: "state", header: "State", width: "8%", render: (row) => row.state ?? "-" },
    {
      key: "reason",
      header: "Reason",
      width: "22%",
      render: (row) => <Badge colorPalette={reasonColor(row.reason)}>{row.reason}</Badge>,
    },
    { key: "from", header: "From", width: "8%", align: "end", render: (row) => row.from ?? "-" },
    { key: "to", header: "To", width: "8%", align: "end", render: (row) => row.to ?? "-" },
    {
      key: "score",
      header: "Score",
      width: "10%",
      align: "end",
      render: (row) =>
        typeof row.scorePermille === "number"
          ? `${row.scorePermille}/1000`
          : "-",
    },
    {
      key: "duration",
      header: "Duration",
      width: "10%",
      align: "end",
      render: (row) => (typeof row.durationMs === "number" ? `${row.durationMs}ms` : "-"),
    },
  ];

  return (
    <VirtualTable
      title="Channel Events"
      countText={`Recent ${rows.length} / 500`}
      action={
        <Button {...toolbarActionButtonProps} onClick={handleExport}>
          <GrDocumentDownload />
          Export
        </Button>
      }
      items={rows}
      columns={columns}
      rowKey={(row) => row.id}
      maxHeight={fillHeight ? "100%" : 530}
    />
  );
}
