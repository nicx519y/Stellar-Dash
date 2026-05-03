import {
  Badge,
  Box,
  Button,
  Card,
  Flex,
  Heading,
  HStack,
  Text,
  VStack,
} from "@chakra-ui/react";
import { useMemo, useState } from "react";

import type { MonitorEvent } from "../../../shared/monitor-types";
import { useMonitorStream } from "./useMonitorStream";
import { PacketsPanel } from "./PacketsPanel";
import { ErrorsPanel } from "./ErrorsPanel";
import { RatePanel } from "./RatePanel";

function latestStatus(events: MonitorEvent[], mode: "USB" | "RF24G") {
  for (let i = events.length - 1; i >= 0; i--) {
    const ev = events[i];
    if (ev.kind === "device_status" && ev.mode === mode) {
      return ev;
    }
  }
  return null;
}

function badgeColor(state: string) {
  if (state === "Connected") return "green";
  if (state === "Connecting") return "yellow";
  if (state === "Error") return "red";
  return "gray";
}

export function App() {
  const { events, packets, errors, latency, rateSeries, paused, setPaused, clear } = useMonitorStream();
  const [tab, setTab] = useState<"packets" | "rate" | "errors">("packets");

  const usbStatus = useMemo(() => latestStatus(events, "USB"), [events]);
  const rfStatus = useMemo(() => latestStatus(events, "RF24G"), [events]);
  const reportHz = latency.estimatedHz > 0 ? latency.estimatedHz : Math.max(packets.usbTxPerSec, packets.rfRxPerSec);

  return (
    <Box w="100vw" h="100vh" bg="#0b0f16" color="gray.50" overflow="hidden">
      <Flex
        px={4}
        py={3}
        align="center"
        justify="space-between"
        borderBottomWidth="1px"
        borderColor="rgba(255,255,255,0.08)"
        bg="rgba(0,0,0,0.15)"
      >
        <VStack align="start" gap={0}>
          <Heading size="sm" letterSpacing="wide">
            connect-monitor
          </Heading>
          <Text fontSize="sm" color="gray.400">
            HID telemetry
          </Text>
        </VStack>
        <HStack gap={3}>
          <Button
            size="sm"
            variant={paused ? "solid" : "outline"}
            colorPalette={paused ? "yellow" : "gray"}
            onClick={() => setPaused(!paused)}
          >
            {paused ? "开始" : "暂停"}
          </Button>
          <Button size="sm" variant="outline" onClick={clear}>
            清空
          </Button>
        </HStack>
      </Flex>

      <Box px={4} py={4} overflow="auto" h="calc(100vh - 64px)">
        <Box
          display="grid"
          gridTemplateColumns={{ base: "1fr", md: "1fr 1fr", lg: "repeat(4, 1fr)" }}
          gap={3}
          mb={4}
        >
          <Card.Root variant="outline" bg="rgba(255,255,255,0.03)" borderColor="rgba(255,255,255,0.08)">
            <Card.Body>
              <Text fontSize="sm" color="gray.400">
                USB 链路
              </Text>
              <HStack mt={2} justify="space-between">
                <Badge colorPalette={badgeColor(usbStatus?.state ?? "Disconnected")}>
                  {usbStatus?.state ?? "Disconnected"}
                </Badge>
                <Text fontSize="sm" color="gray.400">
                  Target {usbStatus?.targetRateHz ?? 0} Hz
                </Text>
              </HStack>
              <Heading size="lg" mt={3}>
                {packets.usbTxPerSec.toFixed(1)}
              </Heading>
              <Text fontSize="sm" color="gray.400">
                pkt/s
              </Text>
            </Card.Body>
          </Card.Root>

          <Card.Root variant="outline" bg="rgba(255,255,255,0.03)" borderColor="rgba(255,255,255,0.08)">
            <Card.Body>
              <Text fontSize="sm" color="gray.400">
                RF 链路
              </Text>
              <HStack mt={2} justify="space-between">
                <Badge colorPalette={badgeColor(rfStatus?.state ?? "Disconnected")}>
                  {rfStatus?.state ?? "Disconnected"}
                </Badge>
                <Text fontSize="sm" color="gray.400">
                  Target {rfStatus?.targetRateHz ?? 0} Hz
                </Text>
              </HStack>
              <Heading size="lg" mt={3}>
                {packets.rfRxPerSec.toFixed(1)}
              </Heading>
              <Text fontSize="sm" color="gray.400">
                pkt/s
              </Text>
            </Card.Body>
          </Card.Root>

          <Card.Root variant="outline" bg="rgba(255,255,255,0.03)" borderColor="rgba(255,255,255,0.08)">
            <Card.Body>
              <Text fontSize="sm" color="gray.400">
                上报率
              </Text>
              <Text fontSize="sm" color="gray.400" mt={2}>
                latency.seq / 监控包速率
              </Text>
              <Heading size="lg" mt={3}>
                {reportHz.toFixed(1)}
              </Heading>
              <Text fontSize="sm" color="gray.400">
                Hz
              </Text>
            </Card.Body>
          </Card.Root>

          <Card.Root
            variant="outline"
            bg="rgba(255,255,255,0.03)"
            borderColor={errors.count > 0 ? "rgba(245,101,101,0.45)" : "rgba(255,255,255,0.08)"}
          >
            <Card.Body>
              <Text fontSize="sm" color="gray.400">
                错误
              </Text>
              <Text fontSize="sm" color="gray.400" mt={2}>
                最近 {errors.windowSec}s
              </Text>
              <Heading size="lg" mt={3}>
                {errors.count}
              </Heading>
              <Text fontSize="sm" color="gray.400">
                条
              </Text>
            </Card.Body>
          </Card.Root>
        </Box>

        <Card.Root variant="outline" bg="rgba(255,255,255,0.02)" borderColor="rgba(255,255,255,0.08)">
          <Card.Body>
            <HStack gap={2}>
              <Button
                size="sm"
                variant={tab === "packets" ? "solid" : "ghost"}
                colorPalette={tab === "packets" ? "blue" : "gray"}
                onClick={() => setTab("packets")}
              >
                通信包
              </Button>
              <Button
                size="sm"
                variant={tab === "rate" ? "solid" : "ghost"}
                colorPalette={tab === "rate" ? "blue" : "gray"}
                onClick={() => setTab("rate")}
              >
                上报率
              </Button>
              <Button
                size="sm"
                variant={tab === "errors" ? "solid" : "ghost"}
                colorPalette={tab === "errors" ? "blue" : "gray"}
                onClick={() => setTab("errors")}
              >
                报错
              </Button>
            </HStack>

            <Box pt={4}>
              {tab === "packets" ? (
                <PacketsPanel items={packets.items} />
              ) : tab === "rate" ? (
                <RatePanel packets={packets} latency={latency} rateSeries={rateSeries} usbStatus={usbStatus} rfStatus={rfStatus} />
              ) : (
                <ErrorsPanel items={errors.items} />
              )}
            </Box>
          </Card.Body>
        </Card.Root>
      </Box>
    </Box>
  );
}
