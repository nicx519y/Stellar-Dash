export type LatencyTableBadgeColor = "gray" | "green" | "yellow";

export type LatencyTableSummary = {
  visibleCount: number;
  maxRows: number;
  headerText: string;
  statusText: string;
  splitLabel: string;
  badgeColor: LatencyTableBadgeColor;
};

export type LatencyTableRow = {
  key: string;
  buttonLabel: string;
  stm32Text: string;
  txText: string;
  rxIrqText: string;
  rxDecodeText: string;
  rxEpWaitText: string;
  rxSubmitText: string;
  rxText: string;
  totalText: string;
};

export type LatencyTableSnapshot = LatencyTableSummary & {
  rows: LatencyTableRow[];
};
