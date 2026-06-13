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
  if (text.includes("disconnect") || text.includes("reconnect")) return "red";
  if (text.includes("high packet loss")) return "yellow";
  if (text.includes("hop")) return "blue";
  return "gray";
}

export function ChannelPanel({ items }: { items: ChannelSwitchRow[] }) {
  const rows = items.slice(-500).reverse();
  const handleExport = () => {
    void exportMarkdown("channel-event-log.md", buildChannelEventLogMarkdown(items));
  };
  const columns: Array<VirtualColumn<ChannelSwitchRow>> = [
    { key: "time", header: "Time", width: "130px", render: (row) => fmtTime(row.timestampMs) },
    { key: "state", header: "State", width: "70px", render: (row) => row.state ?? "-" },
    { key: "from", header: "From", width: "70px", align: "end", render: (row) => row.from ?? "-" },
    { key: "to", header: "To", width: "70px", align: "end", render: (row) => row.to ?? "-" },
    { key: "target", header: "Target", width: "80px", align: "end", render: (row) => row.target ?? "-" },
    {
      key: "reason",
      header: "Reason",
      width: "260px",
      render: (row) => <Badge colorPalette={reasonColor(row.reason)}>{row.reason}</Badge>,
    },
    {
      key: "score",
      header: "Score",
      width: "110px",
      align: "end",
      render: (row) =>
        typeof row.scorePermille === "number"
          ? `${row.scorePermille}/1000`
          : typeof row.lossPercent === "number"
            ? `${(row.lossPercent * 10).toFixed(0)}/1000`
            : "-",
    },
    {
      key: "duration",
      header: "Duration",
      width: "100px",
      align: "end",
      render: (row) => (typeof row.durationMs === "number" ? `${row.durationMs}ms` : "-"),
    },
    {
      key: "loss",
      header: "Loss",
      width: "90px",
      align: "end",
      render: (row) => (typeof row.lossPercent === "number" ? `${row.lossPercent.toFixed(2)}%` : "-"),
    },
    {
      key: "rate",
      header: "Rate",
      width: "100px",
      align: "end",
      render: (row) => (typeof row.rateHz === "number" ? `${row.rateHz.toFixed(1)}Hz` : "-"),
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
    />
  );
}
