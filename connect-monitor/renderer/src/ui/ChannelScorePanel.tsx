import { Badge, Box, HStack, Text, VStack } from "@chakra-ui/react";

import type { ChannelScoreRow } from "./useMonitorStream";
import { neonGreen, PanelHeader, panelSurfaceProps } from "./panelStyles";

function formatAge(updatedAtMs?: number) {
  if (!updatedAtMs) return "Waiting";
  const ageMs = Math.max(0, Date.now() - updatedAtMs);
  if (ageMs < 1000) return "Live";
  return `${(ageMs / 1000).toFixed(1)}s`;
}

function scoreColor(score: number) {
  if (score <= 120) return "green";
  if (score <= 360) return "yellow";
  return "red";
}

export function ChannelScorePanel({ items }: { items: ChannelScoreRow[] }) {
  const updatedAtMs = items[0]?.updatedAtMs;

  return (
    <Box borderWidth="1px" borderRadius="md" overflow="hidden" {...panelSurfaceProps}>
      <PanelHeader title="Channel Scores" meta={formatAge(updatedAtMs)} />
      <VStack align="stretch" gap={2} p={3} minH="530px">
        {items.length === 0 ? (
          <Text fontSize="sm" color="gray.400">
            No score telemetry
          </Text>
        ) : (
          items.map((item) => (
            <Box
              key={item.channel}
              borderWidth="1px"
              borderRadius="md"
              borderColor={item.active ? "rgba(92,255,138,0.58)" : "rgba(92,255,138,0.12)"}
              bg={item.active ? "rgba(92,255,138,0.11)" : "rgba(0,0,0,0.16)"}
              px={3}
              py={2}
              boxShadow={item.active ? "0 0 14px rgba(92,255,138,0.16)" : undefined}
            >
              <HStack justify="space-between" align="center">
                <HStack gap={2}>
                  <Badge colorPalette={item.active ? "green" : "gray"}>#{item.rank}</Badge>
                  <Text fontSize="sm" color="gray.100" fontWeight="semibold">
                    CH {item.channel}
                  </Text>
                </HStack>
                <Badge colorPalette={scoreColor(item.score)}>
                  {item.score}
                </Badge>
              </HStack>
              <Box mt={2} h="5px" borderRadius="999px" bg="rgba(255,255,255,0.08)" overflow="hidden">
                <Box
                  h="100%"
                  w={`${Math.max(2, Math.min(100, item.score / 10))}%`}
                  bg={item.score <= 120 ? neonGreen : item.score <= 360 ? "rgba(255,214,92,0.86)" : "rgba(255,96,96,0.86)"}
                />
              </Box>
            </Box>
          ))
        )}
      </VStack>
    </Box>
  );
}
