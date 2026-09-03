import type { ButtonLatencyEvent, ButtonLatencyStatusEvent } from "../../../shared/monitor-types";
import { HITBOX_BUTTON_MAP, UNMAPPED_GAMEPAD_BUTTON } from "./hitboxButtonMap";
import type { LatencyTableBadgeColor, LatencyTableRow, LatencyTableSnapshot, LatencyTableSummary } from "./latencyTableTypes";

const MAX_LATENCY_ROWS = 300;

const standardButtonLabels = new Map<number, string>();
for (const button of HITBOX_BUTTON_MAP) {
  if (button.gamepadButtonIndex !== UNMAPPED_GAMEPAD_BUTTON && button.label && !standardButtonLabels.has(button.gamepadButtonIndex)) {
    standardButtonLabels.set(button.gamepadButtonIndex, button.label);
  }
}

function formatLatency(value: number) {
  return value < 10 ? value.toFixed(2) : value.toFixed(1);
}

function formatLatencyPart(value: number | undefined) {
  if (typeof value !== "number") return "-";
  if (value < 1) return `${Math.round(value * 1000)}us`;
  return `${formatLatency(value)}ms`;
}

function changedButtonLabels(row: ButtonLatencyEvent) {
  const changed = (row.previousStandardMask ^ row.standardMask) >>> 0;
  const labels: string[] = [];
  for (let bit = 0; bit < 17; bit += 1) {
    if ((changed & (1 << bit)) !== 0) {
      labels.push(standardButtonLabels.get(bit) ?? `B${bit}`);
    }
  }
  return labels.length > 0 ? labels.join("+") : "State";
}

function hasChangedButtons(row: ButtonLatencyEvent) {
  return ((row.previousStandardMask ^ row.standardMask) >>> 0) !== 0;
}

function statusColor(status: ButtonLatencyStatusEvent["status"] | undefined): LatencyTableBadgeColor {
  if (status === "Locked" || status === "Live") return "green";
  if (status === "Syncing" || status === "No match" || status === "Waiting edge") return "yellow";
  return "gray";
}

function latencyRowKey(row: ButtonLatencyEvent) {
  return `${row.timestampMs}-${row.inputSeq}-${row.sampleTickUs}`;
}

function toLatencyTableRow(row: ButtonLatencyEvent): LatencyTableRow {
  return {
    key: latencyRowKey(row),
    buttonLabel: changedButtonLabels(row),
    stm32Text: formatLatencyPart(row.stm32Ms),
    txText: formatLatencyPart(row.txMs),
    rxIrqText: formatLatencyPart(row.rxIrqMs),
    rxDecodeText: formatLatencyPart(row.rxDecodeMs),
    rxEpWaitText: formatLatencyPart(row.rxEpWaitMs),
    rxSubmitText: formatLatencyPart(row.rxSubmitMs),
    rxText: formatLatencyPart(row.rxMs),
    totalText: `${formatLatency(row.latencyMs)}ms`,
  };
}

export function buildLatencyTableSnapshot(
  rows: ButtonLatencyEvent[],
  status: ButtonLatencyStatusEvent | null,
): LatencyTableSnapshot {
  const visibleRows = rows.filter(hasChangedButtons);
  return {
    ...buildLatencyTableSummaryFromVisibleRows(visibleRows, status),
    rows: visibleRows.slice(-MAX_LATENCY_ROWS).reverse().map(toLatencyTableRow),
  };
}

export function buildLatencyTableSummary(
  rows: ButtonLatencyEvent[],
  status: ButtonLatencyStatusEvent | null,
): LatencyTableSummary {
  return buildLatencyTableSummaryFromVisibleRows(rows.filter(hasChangedButtons), status);
}

function buildLatencyTableSummaryFromVisibleRows(
  visibleRows: ButtonLatencyEvent[],
  status: ButtonLatencyStatusEvent | null,
): LatencyTableSummary {
  const average = (() => {
    if (visibleRows.length === 0) return null;
    const recent = visibleRows.slice(-50);
    return recent.reduce((sum, row) => sum + row.latencyMs, 0) / recent.length;
  })();
  const latestFrame = visibleRows.length > 0 ? visibleRows[visibleRows.length - 1].latencyFrame : undefined;
  const splitLabel = latestFrame === "RFH_RHL2"
    ? "RHL2 split latency"
    : latestFrame === "RFH_RHL1"
      ? "RHL1: RX split unavailable"
      : "Split latency";

  return {
    visibleCount: visibleRows.length,
    maxRows: MAX_LATENCY_ROWS,
    headerText: average === null ? (status?.status ?? "Waiting edge") : `${formatLatency(average)}ms`,
    statusText: status?.status ?? "Waiting edge",
    splitLabel,
    badgeColor: average === null ? statusColor(status?.status) : "green",
  };
}
