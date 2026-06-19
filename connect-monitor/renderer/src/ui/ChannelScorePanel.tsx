import { Badge, Box, Button, HStack, Text, VStack } from "@chakra-ui/react";

import type { ChannelScoreRow } from "./useMonitorStream";
import { neonGreen, PanelHeader, panelSurfaceProps } from "./panelStyles";
import { scrollbarStyle } from "./scrollbarStyle";

function scoreColor(score: number) {
  if (score <= 120) return "green";
  if (score <= 360) return "yellow";
  return "red";
}

export function ChannelScorePanel({
  items,
  fillHeight = false,
  autoHopEnabled,
  onAutoHopChange,
  onManualChannelSelect,
}: {
  items: ChannelScoreRow[];
  fillHeight?: boolean;
  autoHopEnabled: boolean;
  onAutoHopChange: (enabled: boolean) => void;
  onManualChannelSelect: (channel: number) => void;
}) {
  const activeBorder = autoHopEnabled ? "rgba(92,255,138,0.58)" : "rgba(96,165,250,0.66)";
  const activeBg = autoHopEnabled ? "rgba(92,255,138,0.11)" : "rgba(96,165,250,0.14)";
  const activeShadow = autoHopEnabled ? "0 0 14px rgba(92,255,138,0.16)" : "0 0 16px rgba(96,165,250,0.22)";
  const activePalette = autoHopEnabled ? "green" : "blue";

  return (
    <Box
      borderWidth="1px"
      borderRadius="md"
      overflow="hidden"
      h={fillHeight ? "100%" : undefined}
      display="flex"
      flexDirection="column"
      minH={0}
      {...panelSurfaceProps}
    >
      <PanelHeader
        title="Channel Scores"
        action={
          <Button
            size="xs"
            h="24px"
            minW="52px"
            borderRadius="6px"
            variant="outline"
            colorPalette={autoHopEnabled ? "green" : "blue"}
            borderColor={autoHopEnabled ? "rgba(92,255,138,0.62)" : "rgba(96,165,250,0.66)"}
            bg={autoHopEnabled ? "rgba(92,255,138,0.16)" : "rgba(96,165,250,0.15)"}
            color={autoHopEnabled ? neonGreen : "blue.200"}
            fontSize="11px"
            lineHeight={1}
            onClick={() => onAutoHopChange(!autoHopEnabled)}
          >
            auto
          </Button>
        }
        borderBottom
        compact
      />
      <VStack
        align="stretch"
        gap={2}
        p={3}
        minH={fillHeight ? 0 : "530px"}
        flex={fillHeight ? "1" : undefined}
        overflowY={fillHeight ? "auto" : undefined}
        css={fillHeight ? scrollbarStyle : undefined}
      >
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
              borderColor={item.active ? activeBorder : "rgba(92,255,138,0.12)"}
              bg={item.active ? activeBg : "rgba(0,0,0,0.16)"}
              px={3}
              py={2}
              boxShadow={item.active ? activeShadow : undefined}
              cursor={autoHopEnabled ? "default" : "pointer"}
              _hover={autoHopEnabled ? undefined : { borderColor: "rgba(96,165,250,0.5)", bg: item.active ? activeBg : "rgba(96,165,250,0.08)" }}
              onClick={() => {
                if (!autoHopEnabled) {
                  onManualChannelSelect(item.channel);
                }
              }}
            >
              <HStack justify="space-between" align="center">
                <HStack gap={2}>
                  <Badge colorPalette={item.active ? activePalette : "gray"}>#{item.rank}</Badge>
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
