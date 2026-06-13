import { Badge, Button } from "@chakra-ui/react";
import { GrDocumentDownload } from "react-icons/gr";

import type { ErrorEvent } from "../../../shared/monitor-types";
import { buildErrorLogMarkdown, exportMarkdown } from "./logExport";
import { toolbarActionButtonProps } from "./panelStyles";
import { VirtualTable, type VirtualColumn } from "./VirtualTable";

function fmtTime(ms: number) {
  const d = new Date(ms);
  return d.toLocaleTimeString() + "." + String(d.getMilliseconds()).padStart(3, "0");
}

function levelColor(level: string) {
  if (level === "FATAL") return "red";
  if (level === "ERROR") return "red";
  if (level === "WARN") return "yellow";
  return "gray";
}

export function ErrorsPanel({ items }: { items: Array<ErrorEvent & { id?: string }> }) {
  const rows = items.slice(-500).reverse();
  const handleExport = () => {
    void exportMarkdown("error-log.md", buildErrorLogMarkdown(items));
  };
  const columns: Array<VirtualColumn<ErrorEvent & { id?: string }>> = [
    { key: "time", header: "Time", width: "16%", render: (e) => fmtTime(e.timestampMs) },
    {
      key: "level",
      header: "Level",
      width: "10%",
      render: (e) => <Badge colorPalette={levelColor(e.level)}>{e.level}</Badge>,
    },
    { key: "source", header: "Source", width: "16%", render: (e) => e.source },
    { key: "code", header: "Code", width: "18%", render: (e) => e.code },
    { key: "message", header: "Message", width: "40%", render: (e) => e.message },
  ];

  return (
    <VirtualTable
      title="Errors"
      countText={`Recent ${rows.length} / 500`}
      action={
        <Button {...toolbarActionButtonProps} onClick={handleExport}>
          <GrDocumentDownload />
          Export
        </Button>
      }
      items={rows}
      columns={columns}
      rowKey={(e, idx) => e.id ?? `${e.timestampMs}-${idx}`}
    />
  );
}
