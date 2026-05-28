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
  if (reason.includes("disconnect") || reason.includes("reconnect")) return "red";
  if (reason.includes("high packet loss")) return "yellow";
  if (reason.includes("hop")) return "blue";
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
      width: "180px",
      render: (row) => <Badge colorPalette={reasonColor(row.reason)}>{row.reason}</Badge>,
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
