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
      const label = standardButtonLabels.get(bit) ?? `B${bit}`;
      labels.push(`${label}${(row.standardMask & (1 << bit)) !== 0 ? "↓" : "↑"}`);
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
    key: row.traceId ?? latencyRowKey(row),
    relativeTexts: Array.from({length:8},(_,i)=>formatLatencyPart(typeof row.relativeStagesUs?.[i] === "number" ? row.relativeStagesUs[i]!/1000 : undefined)),
    buttonLabel: changedButtonLabels(row),
    stm32Text: (row.latencyStageFlags ?? 0) & 2 ? "SAT ≈6ms" : formatLatencyPart(row.stm32Ms),
    txText: (row.latencyStageFlags ?? 0) & 4 ? "SAT ≈6ms" : formatLatencyPart(row.txMs),
    rxIrqText: formatLatencyPart(row.rxIrqMs),
    rxDecodeText: formatLatencyPart(row.rxDecodeMs),
    rxEpWaitText: formatLatencyPart(row.rxEpWaitMs),
    rxSubmitText: formatLatencyPart(row.rxSubmitMs),
    rxText: (row.latencyStageFlags ?? 0) & 8 ? "SAT" : formatLatencyPart(row.rxMs),
    totalText: row.measurement === "usb" ? (row.latencyMs === null ? (row.measurementReason ?? "Incomplete") : `≈${formatLatency(row.latencyMs)}ms`) : row.measurement === "windows" && row.latencyMinMs !== undefined && row.latencyMaxMs !== undefined
      ? `${formatLatency(row.latencyMinMs)}–${formatLatency(row.latencyMaxMs)}ms` : row.measurement === "trace" ? (row.measurementReason ?? "No match") : "—",
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
  const usb = visibleRows.filter(row=>row.measurement === "usb");
  const allComplete = usb.filter(row=>row.latencyMs!==null);
  const complete = allComplete.slice(-50);
  if(usb.length || !visibleRows.length) return {
    visibleCount:visibleRows.length,maxRows:MAX_LATENCY_ROWS,
    headerText:complete.length ? `≈${formatLatency(complete.reduce((sum,row)=>sum+row.latencyMs!,0)/complete.length)}ms` : "No complete USB measurement",
    statusText:usb.length ? `${usb.length} state changes · ${allComplete.length} complete · ${usb.length-allComplete.length} partial` : "No state changes · enable HID and Latency to capture",
    splitLabel:"Sampling → USB IN complete • RF/IRQ boundary estimated • excludes Windows/game processing",
    badgeColor:complete.length ? "yellow" : "gray",
  };
  const recent = visibleRows.filter(row => row.measurement === "windows").slice(-50);
  const average = recent.length ? recent.reduce((sum,row)=>sum+(row.latencyMs ?? 0),0)/recent.length : null;
  const bounds = recent.length ? [
    recent.reduce((sum,row)=>sum+(row.latencyMinMs ?? 0),0)/recent.length,
    recent.reduce((sum,row)=>sum+(row.latencyMaxMs ?? 0),0)/recent.length] : null;
  const splitLabel = "Sampling → Windows XInput • sync/poll uncertainty included • stage rows are not end-to-end";

  return {
    visibleCount: visibleRows.length,
    maxRows: MAX_LATENCY_ROWS,
    headerText: bounds ? `${formatLatency(bounds[0])}–${formatLatency(bounds[1])}ms` : "No Windows measurement",
    statusText: status?.status === "Syncing" && status.clockWidthUs !== undefined
      ? `Syncing ±${formatLatency(Math.abs(status.clockWidthUs)/2000)}ms` : status?.status ?? "Waiting edge",
    splitLabel,
    badgeColor: average === null ? statusColor(status?.status) : "yellow",
  };
}
