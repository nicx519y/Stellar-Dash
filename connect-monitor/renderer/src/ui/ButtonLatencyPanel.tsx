import { Badge, Box, Card, HStack, Text } from "@chakra-ui/react";
import * as React from "react";

import type { ButtonLatencyEvent, ButtonLatencyStatusEvent } from "../../../shared/monitor-types";
import { HITBOX_BUTTON_MAP, UNMAPPED_GAMEPAD_BUTTON } from "./hitboxButtonMap";
import { PanelHeader, panelSurfaceProps } from "./panelStyles";
import { scrollbarStyle } from "./scrollbarStyle";

const MAX_LATENCY_ROWS = 300;
const LATENCY_ROW_HEIGHT = 62;
const LATENCY_ROW_OVERSCAN = 5;
const standardButtonLabels = new Map<number, string>();
for (const button of HITBOX_BUTTON_MAP) {
  if (button.gamepadButtonIndex !== UNMAPPED_GAMEPAD_BUTTON && button.label && !standardButtonLabels.has(button.gamepadButtonIndex)) {
    standardButtonLabels.set(button.gamepadButtonIndex, button.label);
  }
}

function formatLatency(value: number) {
  return value < 10 ? value.toFixed(2) : value.toFixed(1);
}

function formatTime(ms: number) {
  const d = new Date(ms);
  return `${d.toLocaleTimeString()}.${String(d.getMilliseconds()).padStart(3, "0")}`;
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

function statusColor(status: ButtonLatencyStatusEvent["status"] | undefined) {
  if (status === "Locked" || status === "Live") return "green";
  if (status === "Syncing" || status === "No match" || status === "Waiting edge") return "yellow";
  return "gray";
}

function clamp(value: number, min: number, max: number) {
  return Math.min(max, Math.max(min, value));
}

function latencyRowKey(row: ButtonLatencyEvent) {
  return `${row.timestampMs}-${row.inputSeq}-${row.sampleTickUs}`;
}

function LatencyVirtualList({ rows }: { rows: ButtonLatencyEvent[] }) {
  const scrollRef = React.useRef<HTMLDivElement | null>(null);
  const [scrollTop, setScrollTop] = React.useState(0);
  const [viewportHeight, setViewportHeight] = React.useState(0);

  React.useLayoutEffect(() => {
    const scroller = scrollRef.current;
    if (!scroller) return;

    const updateViewportHeight = () => {
      setViewportHeight(scroller.clientHeight);
    };
    updateViewportHeight();

    const resizeObserver = new ResizeObserver(updateViewportHeight);
    resizeObserver.observe(scroller);
    return () => resizeObserver.disconnect();
  }, []);

  if (rows.length === 0) {
    return (
      <Box flex="1" minH={0} overflowY="auto" css={scrollbarStyle}>
        <Box px={3} py={4}>
          <Text fontSize="sm" color="gray.400">
            No latency samples
          </Text>
        </Box>
      </Box>
    );
  }

  const totalHeight = rows.length * LATENCY_ROW_HEIGHT;
  const effectiveViewportHeight = viewportHeight || LATENCY_ROW_HEIGHT * 6;
  const maxScrollTop = Math.max(0, totalHeight - effectiveViewportHeight);
  const effectiveScrollTop = clamp(scrollTop, 0, maxScrollTop);
  const visibleCapacity = Math.ceil(effectiveViewportHeight / LATENCY_ROW_HEIGHT);
  const poolSize = visibleCapacity + LATENCY_ROW_OVERSCAN * 2;
  const maxStartIndex = Math.max(0, rows.length - poolSize);
  const startIndex = clamp(Math.floor(effectiveScrollTop / LATENCY_ROW_HEIGHT) - LATENCY_ROW_OVERSCAN, 0, maxStartIndex);
  const endIndex = Math.min(rows.length, startIndex + poolSize);
  const visibleRows = rows.slice(startIndex, endIndex);

  return (
    <Box
      ref={scrollRef}
      flex="1"
      minH={0}
      overflowY="auto"
      position="relative"
      css={scrollbarStyle}
      onScroll={(event) => setScrollTop(event.currentTarget.scrollTop)}
    >
      <Box h={`${totalHeight}px`} minH="100%" position="relative">
        <Box position="absolute" top={`${startIndex * LATENCY_ROW_HEIGHT}px`} left={0} right={0}>
          {visibleRows.map((row, offset) => {
            const index = startIndex + offset;
            return (
              <Box
                key={latencyRowKey(row)}
                h={`${LATENCY_ROW_HEIGHT}px`}
                px={3}
                py={2}
                borderBottomWidth="1px"
                borderColor="rgba(92,255,138,0.08)"
                bg={index % 2 === 0 ? "rgba(0,0,0,0.12)" : "rgba(92,255,138,0.035)"}
              >
                <HStack justify="space-between" align="center" gap={2}>
                  <Text fontSize="12px" color="gray.100" fontWeight="semibold" minW={0} overflow="hidden" textOverflow="ellipsis" whiteSpace="nowrap">
                    {changedButtonLabels(row)}
                  </Text>
                  <Badge colorPalette={row.confidence === "high" ? "green" : row.confidence === "medium" ? "yellow" : "gray"}>
                    {formatLatency(row.latencyMs)}ms
                  </Badge>
                </HStack>
                <HStack justify="space-between" mt={1} gap={2}>
                  <Text fontSize="10px" color="gray.500">
                    {formatTime(row.timestampMs)}
                  </Text>
                  <Text fontSize="10px" color="gray.500">
                    seq {row.inputSeq}
                  </Text>
                </HStack>
              </Box>
            );
          })}
        </Box>
      </Box>
    </Box>
  );
}

export function ButtonLatencyPanel({
  rows,
  status,
}: {
  rows: ButtonLatencyEvent[];
  status: ButtonLatencyStatusEvent | null;
}) {
  const visibleRows = React.useMemo(() => rows.filter(hasChangedButtons), [rows]);
  const average = React.useMemo(() => {
    if (visibleRows.length === 0) return null;
    const recent = visibleRows.slice(-50);
    return recent.reduce((sum, row) => sum + row.latencyMs, 0) / recent.length;
  }, [visibleRows]);
  const displayRows = visibleRows.slice(-MAX_LATENCY_ROWS).reverse();
  const headerText = average === null ? (status?.status ?? "Waiting edge") : `${formatLatency(average)}ms`;

  return (
    <Card.Root variant="outline" overflow="hidden" h="100%" minW="250px" w="250px" {...panelSurfaceProps}>
      <PanelHeader
        title="Latency"
        meta={`${visibleRows.length}/${MAX_LATENCY_ROWS}`}
        action={<Badge colorPalette={average === null ? statusColor(status?.status) : "green"}>{headerText}</Badge>}
        borderBottom
        compact
      />
      <Card.Body p={0} minH={0} display="flex" flexDirection="column">
        <Box px={3} py={2} borderBottomWidth="1px" borderColor="rgba(92,255,138,0.08)">
          <HStack justify="space-between" gap={2}>
            <Text fontSize="10px" color="gray.500">
              {status?.status ?? "Waiting edge"}
            </Text>
            <Text fontSize="10px" color="gray.500">
              RX firmware
            </Text>
          </HStack>
        </Box>
        <LatencyVirtualList rows={displayRows} />
      </Card.Body>
    </Card.Root>
  );
}
