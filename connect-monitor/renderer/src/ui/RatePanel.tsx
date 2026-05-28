import { Card } from "@chakra-ui/react";

import type { DeviceStatusEvent, PacketEvent } from "../../../shared/monitor-types";
import type { ChannelSwitchRow } from "./useMonitorStream";
import type { LossPoint, RatePoint } from "./TelemetryTrendChart";
import { TelemetryTrendChart } from "./TelemetryTrendChart";
import { PanelHeader, panelSurfaceProps } from "./panelStyles";

export function RatePanel({
  packets,
  latency,
  rateSeries,
  lossSeries,
  channelSwitches,
  rfStatus,
}: {
  packets: { items: Array<PacketEvent & { id?: string }>; usbTxPerSec: number; rfRxPerSec: number };
  latency: { estimatedHz: number; lastSeq: number; lastAtMs: number };
  rateSeries: RatePoint[];
  lossSeries: LossPoint[];
  channelSwitches: ChannelSwitchRow[];
  rfStatus: DeviceStatusEvent | null;
}) {
  const fallbackHz = Math.max(packets.usbTxPerSec, packets.rfRxPerSec);
  const rfActualHz = rfStatus?.actualRateHz ?? 0;
  const reportHz = rfActualHz > 0 ? rfActualHz : latency.estimatedHz > 0 ? latency.estimatedHz : fallbackHz;
  const latestLoss = lossSeries.length > 0 ? lossSeries[lossSeries.length - 1].value : 0;

  return (
    <Card.Root variant="outline" {...panelSurfaceProps}>
      <PanelHeader title="Report Rate / Packet Loss / Channel Events" meta={`${reportHz.toFixed(1)} Hz · ${latestLoss.toFixed(2)} %`} />
      <Card.Body px={3} pt={0} pb={3}>
        <TelemetryTrendChart
          rateSeries={rateSeries}
          lossSeries={lossSeries}
          channelSwitches={channelSwitches}
        />
      </Card.Body>
    </Card.Root>
  );
}
