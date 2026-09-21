import { Badge, Box, Button, CloseButton, HStack, Popover, Portal, Switch, Text, VStack } from "@chakra-ui/react";

import type { ChannelScoreRow } from "./useMonitorStream";
import { neonGreen, PanelHeader, panelSurfaceProps, toolbarActionButtonProps } from "./panelStyles";
import { scrollbarStyle } from "./scrollbarStyle";
import type { PacketEvent } from "../../../shared/monitor-types";
import { RfChannelStatus } from "./RfChannelStatus";

function scoreColor(score: number) {
  if (score === 65535) return "gray";
  if (score <= 120) return "green";
  if (score <= 400) return "yellow";
  return "red";
}

export function ChannelScorePanel({
  items,
  fillHeight = false,
  autoHopEnabled,
  onAutoHopChange,
  onManualChannelSelect,
  packets = [],
}: {
  items: ChannelScoreRow[];
  fillHeight?: boolean;
  autoHopEnabled: boolean;
  onAutoHopChange: (enabled: boolean) => void;
  onManualChannelSelect: (channel: number) => void;
  packets?: PacketEvent[];
}) {
  const isV3 = packets.some((packet) => packet.rfChannel !== undefined);
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
        title="Channel"
        action={
          <HStack gap={2}>
            <Switch.Root
              checked={autoHopEnabled}
              colorPalette={autoHopEnabled ? "green" : "blue"}
              size="sm"
              display="flex"
              alignItems="center"
              gap={2}
              onCheckedChange={(details) => onAutoHopChange(details.checked)}
            >
              <Switch.HiddenInput />
              <Switch.Control borderColor={autoHopEnabled ? "rgba(92,255,138,0.66)" : "rgba(96,165,250,0.66)"}>
                <Switch.Thumb />
              </Switch.Control>
              <Switch.Label fontSize="11px" color={autoHopEnabled ? neonGreen : "blue.200"}>
                auto
              </Switch.Label>
            </Switch.Root>
            <Popover.Root positioning={{ placement: "bottom-end" }} lazyMount unmountOnExit>
              <Popover.Trigger asChild>
                <Button {...toolbarActionButtonProps} px={2} aria-label="Channel debug">
                  debug
                </Button>
              </Popover.Trigger>
              <Portal>
                <Popover.Positioner>
                  <Popover.Content
                    w="420px"
                    maxW="calc(100vw - 24px)"
                    maxH="min(680px, calc(100vh - 24px))"
                    bg="#08121b"
                    color="gray.100"
                    borderColor="rgba(92,255,138,0.3)"
                    boxShadow="0 18px 48px rgba(0,0,0,0.6)"
                    overflow="hidden"
                  >
                    <Popover.Header borderBottomWidth="1px" borderColor="whiteAlpha.200" pr={10}>
                      <Popover.Title fontSize="sm" fontWeight="semibold">Channel debug</Popover.Title>
                    </Popover.Header>
                    <Popover.CloseTrigger asChild>
                      <CloseButton size="sm" position="absolute" top={1} right={1} aria-label="Close channel debug" />
                    </Popover.CloseTrigger>
                    <Popover.Body p={0} minH={0} overflowY="auto" css={scrollbarStyle}>
                      {isV3 ? (
                        <RfChannelStatus packets={packets} />
                      ) : (
                        <Text p={3} fontSize="sm" color="gray.400">No RF channel debug telemetry</Text>
                      )}
                    </Popover.Body>
                  </Popover.Content>
                </Popover.Positioner>
              </Portal>
            </Popover.Root>
          </HStack>
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
                <Badge colorPalette={isV3 ? (item.score === 65535 ? "gray" : item.score < 20 ? "green" : item.score < 50 ? "yellow" : "red") : scoreColor(item.score)}>
                  {item.score === 65535 ? "Unknown" : isV3 ? `${(item.score / 10).toFixed(1)}%` : item.score}
                </Badge>
              </HStack>
              <Box mt={2} h="5px" borderRadius="999px" bg="rgba(255,255,255,0.08)" overflow="hidden">
                <Box
                  h="100%"
                  w={`${item.score === 65535 ? 0 : Math.max(2, Math.min(100, item.score / 10))}%`}
                  bg={item.score <= 120 ? neonGreen : item.score <= 400 ? "rgba(255,214,92,0.86)" : "rgba(255,96,96,0.86)"}
                />
              </Box>
            </Box>
          ))
        )}
      </VStack>
    </Box>
  );
}
