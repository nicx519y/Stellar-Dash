import type { ErrorEvent, PacketEvent } from "../../../shared/monitor-types";
import type { ChannelSwitchRow } from "./useMonitorStream";

const LOG_LIMIT = 500;

function formatDateTime(timestampMs: number) {
  const d = new Date(timestampMs);
  const date = d.toLocaleDateString();
  const time = d.toLocaleTimeString() + "." + String(d.getMilliseconds()).padStart(3, "0");
  return `${date} ${time}`;
}

function mdCell(value: unknown) {
  if (value === undefined || value === null || value === "") return "-";
  return String(value).replace(/\|/g, "\\|").replace(/\r?\n/g, " ");
}

function markdownTable(headers: string[], rows: unknown[][]) {
  if (rows.length === 0) {
    return "_No entries._";
  }
  const header = `| ${headers.map(mdCell).join(" | ")} |`;
  const separator = `| ${headers.map(() => "---").join(" | ")} |`;
  const body = rows.map((row) => `| ${row.map(mdCell).join(" | ")} |`).join("\n");
  return [header, separator, body].join("\n");
}

function documentHeader(title: string, count: number) {
  return [
    `# ${title}`,
    "",
    `Generated: ${formatDateTime(Date.now())}`,
    `Entries: ${count} / ${LOG_LIMIT}`,
    "",
  ].join("\n");
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

export function exportMarkdown(suggestedFileName: string, content: string) {
  return window.connectMonitorApi?.exportMarkdown?.({ suggestedFileName, content });
}

export function buildPacketLogMarkdown(items: Array<PacketEvent & { id?: string }>) {
  const rows = items.slice(-LOG_LIMIT).reverse();
  const table = markdownTable(
    ["Time", "Type", "Length", "Seq", "Samples", "Loss", "Rate", "Channel No.", "Target", "State", "Payload"],
    rows.map((p) => [
      formatDateTime(p.timestampMs),
      displayMessageType(p.messageType),
      p.payloadLen,
      typeof p.seq === "number" ? p.seq : undefined,
      typeof p.sampleCount === "number" ? `${p.sampleCount}/${p.expectedCount ?? "-"}` : undefined,
      typeof p.lossPermille === "number" ? `${(p.lossPermille / 10).toFixed(2)}%` : undefined,
      typeof p.rateHz === "number" ? `${p.rateHz.toFixed(1)} Hz` : undefined,
      p.channelNumber,
      p.targetChannelNumber,
      p.rfStateCode,
      p.payloadHex,
    ]),
  );
  return `${documentHeader("Packet Log", rows.length)}${table}\n`;
}

export function buildChannelEventLogMarkdown(items: ChannelSwitchRow[]) {
  const rows = items.slice(-LOG_LIMIT).reverse();
  const table = markdownTable(
    ["Time", "Type", "State", "From", "To", "Reason", "Score", "Duration"],
    rows.map((row) => [
      formatDateTime(row.timestampMs),
      row.type,
      row.state,
      row.from,
      row.to,
      row.reason,
      typeof row.scorePermille === "number" ? `${row.scorePermille}/1000` : undefined,
      typeof row.durationMs === "number" ? `${row.durationMs} ms` : undefined,
    ]),
  );
  return `${documentHeader("Channel Event Log", rows.length)}${table}\n`;
}

export function buildErrorLogMarkdown(items: Array<ErrorEvent & { id?: string }>) {
  const rows = items.slice(-LOG_LIMIT).reverse();
  const table = markdownTable(
    ["Time", "Level", "Source", "Code", "Count", "Message"],
    rows.map((e) => [
      formatDateTime(e.timestampMs),
      e.level,
      e.source,
      e.code,
      e.count,
      e.message,
    ]),
  );
  return `${documentHeader("Error Log", rows.length)}${table}\n`;
}
