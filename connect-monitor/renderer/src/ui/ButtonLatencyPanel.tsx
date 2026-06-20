import { Badge, Box, Card, HStack, Text, VStack } from "@chakra-ui/react";
import { useMemo } from "react";

import type { ButtonLatencyEvent, ButtonLatencyStatusEvent } from "../../../shared/monitor-types";
import { HITBOX_BUTTON_MAP, UNMAPPED_GAMEPAD_BUTTON } from "./hitboxButtonMap";
import { PanelHeader, panelSurfaceProps } from "./panelStyles";

const MAX_LATENCY_ROWS = 500;
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

function formatRtt(value: number | undefined) {
  return typeof value === "number" ? `${(value / 1000).toFixed(2)}ms` : "-";
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

function statusColor(status: ButtonLatencyStatusEvent["status"] | undefined) {
  if (status === "Locked") return "green";
  if (status === "Syncing" || status === "No match") return "yellow";
  return "gray";
}

export function ButtonLatencyPanel({
  rows,
  status,
}: {
  rows: ButtonLatencyEvent[];
  status: ButtonLatencyStatusEvent | null;
}) {
  const latest = rows[rows.length - 1];
  const average = useMemo(() => {
    if (rows.length === 0) return null;
    const recent = rows.slice(-50);
    return recent.reduce((sum, row) => sum + row.latencyMs, 0) / recent.length;
  }, [rows]);
  const displayRows = rows.slice(-MAX_LATENCY_ROWS).reverse();
  const headerText = average === null ? (status?.status ?? "Syncing") : `${formatLatency(average)}ms`;

  return (
    <Card.Root variant="outline" overflow="hidden" h="100%" minW="250px" w="250px" {...panelSurfaceProps}>
      <PanelHeader
        title="Button Latency"
        meta={`${rows.length}/${MAX_LATENCY_ROWS}`}
        action={<Badge colorPalette={average === null ? statusColor(status?.status) : "green"}>{headerText}</Badge>}
        borderBottom
        compact
      />
      <Card.Body p={0} minH={0} display="flex" flexDirection="column">
        <Box px={3} py={2} borderBottomWidth="1px" borderColor="rgba(92,255,138,0.08)">
          <HStack justify="space-between" gap={2}>
            <Text fontSize="10px" color="gray.500">
              {status?.status ?? "Syncing"}
            </Text>
            <Text fontSize="10px" color="gray.500">
              RTT {formatRtt(status?.syncRttUs)}
            </Text>
          </HStack>
        </Box>
        <VStack align="stretch" gap={0} flex="1" minH={0} overflowY="auto">
          {displayRows.length === 0 ? (
            <Box px={3} py={4}>
              <Text fontSize="sm" color="gray.400">
                No latency samples
              </Text>
            </Box>
          ) : (
            displayRows.map((row) => (
              <Box
                key={`${row.timestampMs}-${row.inputSeq}-${row.sampleTickUs}`}
                px={3}
                py={2}
                borderBottomWidth="1px"
                borderColor="rgba(92,255,138,0.08)"
                bg="rgba(0,0,0,0.12)"
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
                    seq {row.inputSeq} rtt {formatRtt(row.syncRttUs)}
                  </Text>
                </HStack>
              </Box>
            ))
          )}
        </VStack>
      </Card.Body>
    </Card.Root>
  );
}
