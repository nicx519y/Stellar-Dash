import { Badge, Box, Card, HStack, Text } from "@chakra-ui/react";
import * as React from "react";

import type { ButtonLatencyEvent, ButtonLatencyStatusEvent, LatencyTableBounds } from "../../../shared/monitor-types";
import { buildLatencyTableSummary } from "./latencyTableModel";
import { PanelHeader, panelSurfaceProps } from "./panelStyles";

function hiddenBounds(): LatencyTableBounds {
  return {
    x: 0,
    y: 0,
    width: 1,
    height: 1,
    visible: false,
  };
}

function LatencyTableViewSlot() {
  const slotRef = React.useRef<HTMLDivElement | null>(null);
  const frameRef = React.useRef<number | null>(null);

  const syncBounds = React.useCallback(() => {
    if (frameRef.current !== null) return;
    frameRef.current = window.requestAnimationFrame(() => {
      frameRef.current = null;
      const slot = slotRef.current;
      if (!slot) {
        window.connectMonitorApi?.setLatencyTableBounds?.(hiddenBounds());
        return;
      }

      const rect = slot.getBoundingClientRect();
      const visible =
        rect.width >= 2 &&
        rect.height >= 2 &&
        rect.right > 0 &&
        rect.bottom > 0 &&
        rect.left < window.innerWidth &&
        rect.top < window.innerHeight;

      window.connectMonitorApi?.setLatencyTableBounds?.({
        x: rect.left,
        y: rect.top,
        width: rect.width,
        height: rect.height,
        visible,
      });
    });
  }, []);

  React.useLayoutEffect(() => {
    syncBounds();

    const resizeObserver = new ResizeObserver(syncBounds);
    if (slotRef.current) {
      resizeObserver.observe(slotRef.current);
    }
    resizeObserver.observe(document.body);
    window.addEventListener("resize", syncBounds);
    window.addEventListener("scroll", syncBounds, true);

    return () => {
      resizeObserver.disconnect();
      window.removeEventListener("resize", syncBounds);
      window.removeEventListener("scroll", syncBounds, true);
      if (frameRef.current !== null) {
        window.cancelAnimationFrame(frameRef.current);
        frameRef.current = null;
      }
      window.connectMonitorApi?.setLatencyTableBounds?.(hiddenBounds());
    };
  }, [syncBounds]);

  return (
    <Box
      ref={slotRef}
      flex="1"
      minH={0}
      w="100%"
      position="relative"
      overflow="hidden"
    />
  );
}

export function ButtonLatencyPanel({
  rows,
  status,
}: {
  rows: ButtonLatencyEvent[];
  status: ButtonLatencyStatusEvent | null;
}) {
  const table = React.useMemo(() => buildLatencyTableSummary(rows, status), [rows, status]);

  return (
    <Card.Root variant="outline" overflow="hidden" h="100%" minW={0} w="100%" {...panelSurfaceProps}>
      <PanelHeader
        title="Latency"
        meta={`${table.visibleCount}/${table.maxRows}`}
        action={<Badge colorPalette={table.badgeColor}>{table.headerText}</Badge>}
        borderBottom
        compact
      />
      <Card.Body p={0} minH={0} display="flex" flexDirection="column">
        <Box px={3} py={2} borderBottomWidth="1px" borderColor="rgba(92,255,138,0.08)">
          <HStack justify="space-between" gap={2}>
            <Text fontSize="10px" color="gray.500">
              {table.statusText}
            </Text>
            <Text fontSize="10px" color="gray.500">
              {table.splitLabel}
            </Text>
          </HStack>
        </Box>
        <LatencyTableViewSlot />
      </Card.Body>
    </Card.Root>
  );
}
