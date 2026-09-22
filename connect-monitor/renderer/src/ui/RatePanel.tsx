import { RxPipelineStatus } from "./RxPipelineStatus";
import { txMetricsDelta } from "../../../shared/rf-tx-metrics";
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
  const metricPackets = packets.items.filter(p=>p.rfTxMetrics).slice(-2);
  const metricLatest = metricPackets.at(-1);
  const metricDelta = metricPackets.length===2 && nowMs-metricLatest!.timestampMs<6500
    ? txMetricsDelta(metricPackets[0].rfTxMetrics!,metricLatest!.rfTxMetrics!) : null;
  const d = metricDelta?.totals;
  const protocol = packets.items.filter(p=>p.rfProtocolVersion!==undefined).at(-1);
  const versions = packets.items.filter(p=>p.rfChannel?.versionMismatches!==undefined).slice(-2);
  const mismatchGrowing = versions.length===2 && versions[1].rfChannel!.versionMismatches! > versions[0].rfChannel!.versionMismatches!;


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
        RF sequence gaps: {airText}{metricDelta ? ` · TX accepted 5B/7B/12B in its window: ${d!.packets5}/${d!.packets7}/${d!.packets12}` : ` · TX skipped/legacy window: ${tx?.rfTxDiagnosticAgeMs !== undefined && tx.rfTxDiagnosticAgeMs < 2000 ? tx.rfTxDropped : "不可用"}`}
        {short && nowMs-short.timestampMs<2500 ? ` · TX totals 5B/7B/12B: ${short.rfTx5ByteTotal}/${short.rfTx7ByteTotal}/${short.rfTx12ByteTotal} · ACK reserved slots: ${short.rfAckReservedSlots}` : ""}
      </Text>
      <Text px={3} fontSize="11px" color="gray.400">
        {metricDelta && d
          ? `TX ${(metricDelta.elapsedUs/1e6).toFixed(2)}s: opportunities ${d.due} / attempts ${d.attempt} / accepted ${d.accepted} / start failed ${d.startFailed} · skipped ACK ${d.ackSkip}, transaction ${d.pauseSkip}, busy ${d.busySkip}, guard ${d.guardSkip}, control ${d.controlSkip}, cancelled ${d.cancelSkip} · ACK actual ${metricDelta.ackPercent.toFixed(2)}% / early ${d.earlyRelease} / timeout ${d.ackTimeout} · timer late ${d.late}, missed≈${d.missedEstimate}`
          : "TX 原因 / ACK 实际占用：不可用（需要匹配 v5 固件及两个完整统计快照）"}
      </Text>
      {protocol && (protocol.rfProtocolVersion!==5 || mismatchGrowing) && <Text px={3} fontSize="11px" color="orange.300">
        {mismatchGrowing ? "空口版本不匹配：请匹配更新 TX 与 RX。" : `当前空口 v${protocol.rfProtocolVersion}；新 ACK 优化需要匹配更新 TX/RX 至 v5。`}
      </Text>}
      <RxPipelineStatus packets={packets.items} now={nowMs} />
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
