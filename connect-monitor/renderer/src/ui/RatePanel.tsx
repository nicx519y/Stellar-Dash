import { Card, Text } from "@chakra-ui/react";
import * as React from "react";

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
  const [nowMs, setNowMs] = React.useState(Date.now());
  React.useEffect(() => {
    const timer = window.setInterval(() => setNowMs(Date.now()), 1000);
    return () => window.clearInterval(timer);
  }, []);
  const rfConnected = rfStatus?.state === "Connected";
  const lastStatisticsAt = rateSeries.at(-1)?.tMs ?? 0;
  const statisticsAgeMs = lastStatisticsAt > 0 ? nowMs - lastStatisticsAt : 0;
  const statisticsStale = rfConnected && lastStatisticsAt > 0 && statisticsAgeMs > 2000;
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
  const air = packets.items.filter(p => p.rfAirMissingTotal !== undefined && p.rfAirReceivedTotal !== undefined).slice(-2);
  const last = air[1], prev = air[0];
  const missing = last && prev ? last.rfAirMissingTotal! - prev.rfAirMissingTotal! : -1;
  const received = last && prev ? last.rfAirReceivedTotal! - prev.rfAirReceivedTotal! : -1;
  const fresh = last && nowMs-last.timestampMs < 2500;
  const airText = fresh && missing >= 0 && received >= 0 && missing+received > 0
    ? `${(100*missing/(missing+received)).toFixed(2)}%` : "—";
  const tx = packets.items.filter(p=>p.rfTxDropped!==undefined).at(-1);
  const short = packets.items.filter(p=>p.messageType==="RFH_RHP2").at(-1);

  return (
    <Card.Root variant="outline" h="100%" minW="400px" display="flex" flexDirection="column" {...panelSurfaceProps}>
      <PanelHeader
        title="Report Rate / Input Deficit / Channel Events"
        meta={statisticsStale
          ? `统计停更 ${(statisticsAgeMs / 1000).toFixed(1)}s`
          : `${reportHz.toFixed(1)} Hz · ${latestLoss.toFixed(2)} %`}
        action={onClearData ? <ClearDataIconButton label="Clear chart data" onClick={onClearData} /> : undefined}
        compact={compact}
        borderBottom
      />
      <Text px={3} pt={1} fontSize="11px" color="gray.400">
        RF sequence gaps: {airText} · TX skipped/window: {tx?.rfTxDiagnosticAgeMs !== undefined && tx.rfTxDiagnosticAgeMs < 2000 ? tx.rfTxDropped : "—"}
        {short && nowMs-short.timestampMs<2500 ? ` · TX totals 5B/7B/12B: ${short.rfTx5ByteTotal}/${short.rfTx7ByteTotal}/${short.rfTx12ByteTotal} · ACK reserved slots: ${short.rfAckReservedSlots}` : ""}
      </Text>
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
