import { Card } from "@chakra-ui/react";

import type { DeviceStatusEvent, PacketEvent } from "../../../shared/monitor-types";
import type { ChannelSwitchRow } from "./useMonitorStream";
import type { LossPoint, RatePoint } from "./TelemetryTrendChart";
import { TelemetryTrendChart } from "./TelemetryTrendChart";
import { ClearDataIconButton } from "./panelActions";
import { PanelHeader, panelSurfaceProps } from "./panelStyles";

export function RatePanel({
  packets,
  latency,
  rateSeries,
  lossSeries,
  channelSwitches,
  chartRateSeries,
  chartLossSeries,
  chartChannelSwitches,
  rfStatus,
  chartHeight = 360,
  compact = false,
  onClearData,
}: {
  packets: { items: Array<PacketEvent & { id?: string }>; usbTxPerSec: number; rfRxPerSec: number };
  latency: { estimatedHz: number; lastSeq: number; lastAtMs: number };
  rateSeries: RatePoint[];
  lossSeries: LossPoint[];
  channelSwitches: ChannelSwitchRow[];
  chartRateSeries: RatePoint[];
  chartLossSeries: LossPoint[];
  chartChannelSwitches: ChannelSwitchRow[];
  rfStatus: DeviceStatusEvent | null;
  chartHeight?: number | string;
  compact?: boolean;
  onClearData?: () => void;
}) {
  const rfConnected = rfStatus?.state === "Connected";
  const fallbackHz = Math.max(packets.usbTxPerSec, packets.rfRxPerSec);
  const rfActualHz = rfStatus?.actualRateHz ?? 0;
  const reportHz = rfConnected
    ? rfActualHz > 0
      ? rfActualHz
      : latency.estimatedHz > 0
        ? latency.estimatedHz
        : fallbackHz
    : 0;
  const latestLoss = rfConnected && lossSeries.length > 0 ? lossSeries[lossSeries.length - 1].value : 0;

  return (
    <Card.Root variant="outline" h="100%" minW="400px" display="flex" flexDirection="column" {...panelSurfaceProps}>
      <PanelHeader
        title="Report Rate / Packet Loss / Channel Events"
        meta={`${reportHz.toFixed(1)} Hz · ${latestLoss.toFixed(2)} %`}
        action={onClearData ? <ClearDataIconButton label="Clear chart data" onClick={onClearData} /> : undefined}
        compact={compact}
        borderBottom
      />
      <Card.Body px={3} pt={compact ? 1 : 0} pb={compact ? 2 : 3} flex="1" minH={0} display="flex">
        <TelemetryTrendChart
          rateSeries={chartRateSeries}
          lossSeries={chartLossSeries}
          channelSwitches={chartChannelSwitches}
          height={chartHeight}
        />
      </Card.Body>
    </Card.Root>
  );
}
