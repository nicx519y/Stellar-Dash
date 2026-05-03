import { Card, HStack, Stat, Text, VStack } from "@chakra-ui/react";

import type { DeviceStatusEvent } from "../../../shared/monitor-types";
import type { RatePoint } from "./RateLineChart";
import { RateLineChart } from "./RateLineChart";

export function RatePanel({
  packets,
  latency,
  rateSeries,
  usbStatus,
  rfStatus,
}: {
  packets: { usbTxPerSec: number; rfRxPerSec: number };
  latency: { estimatedHz: number; lastSeq: number; lastAtMs: number };
  rateSeries: RatePoint[];
  usbStatus: DeviceStatusEvent | null;
  rfStatus: DeviceStatusEvent | null;
}) {
  const fallbackHz = Math.max(packets.usbTxPerSec, packets.rfRxPerSec);
  const reportHz = latency.estimatedHz > 0 ? latency.estimatedHz : fallbackHz;

  return (
    <VStack gap={4} align="stretch">
      <RateLineChart points={rateSeries} />
      <HStack gap={4} align="stretch">
        <Card.Root variant="outline" bg="rgba(255,255,255,0.02)" borderColor="rgba(255,255,255,0.08)" flex="1">
          <Card.Body>
            <Stat.Root>
              <Stat.Label>USB 监控包速率</Stat.Label>
              <Stat.ValueText>{packets.usbTxPerSec.toFixed(1)} pkt/s</Stat.ValueText>
              <Stat.HelpText>Target {usbStatus?.targetRateHz ?? 0} Hz</Stat.HelpText>
            </Stat.Root>
          </Card.Body>
        </Card.Root>

        <Card.Root variant="outline" bg="rgba(255,255,255,0.02)" borderColor="rgba(255,255,255,0.08)" flex="1">
          <Card.Body>
            <Stat.Root>
              <Stat.Label>RF 监控包速率</Stat.Label>
              <Stat.ValueText>{packets.rfRxPerSec.toFixed(1)} pkt/s</Stat.ValueText>
              <Stat.HelpText>Target {rfStatus?.targetRateHz ?? 0} Hz</Stat.HelpText>
            </Stat.Root>
          </Card.Body>
        </Card.Root>

        <Card.Root variant="outline" bg="rgba(255,255,255,0.02)" borderColor="rgba(255,255,255,0.08)" flex="1">
          <Card.Body>
            <Stat.Root>
              <Stat.Label>上报率估计</Stat.Label>
              <Stat.ValueText>{reportHz.toFixed(1)} Hz</Stat.ValueText>
              <Stat.HelpText>
                {latency.estimatedHz > 0
                  ? `latency.seq Δ (Seq ${latency.lastSeq} @ ${
                      latency.lastAtMs ? new Date(latency.lastAtMs).toLocaleTimeString() : "-"
                    })`
                  : "1s 滑窗包速率"}
              </Stat.HelpText>
            </Stat.Root>
          </Card.Body>
        </Card.Root>
      </HStack>

      <Card.Root variant="outline" bg="rgba(255,255,255,0.02)" borderColor="rgba(255,255,255,0.08)">
        <Card.Body>
          <Text fontSize="sm" color="gray.300">
            说明：折线图与“上报率”优先使用 latency.seq 增量估计；没有 latency 时回退到 1 秒滑窗的监控包速率（USB TX / RF RX 取较大者）。
          </Text>
        </Card.Body>
      </Card.Root>
    </VStack>
  );
}
