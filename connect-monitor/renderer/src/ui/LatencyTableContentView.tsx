import { Box, Text } from "@chakra-ui/react";
import * as React from "react";

import { buildLatencyTableSnapshot } from "./latencyTableModel";
import type { LatencyTableSnapshot } from "./latencyTableTypes";
import { readCardClearAfter, subscribeCardClear } from "./cardClear";
import { scrollbarStyle } from "./scrollbarStyle";
import { useMonitorStream } from "./useMonitorStream";

const LATENCY_ROW_HEIGHT = 30;
const LATENCY_ROW_OVERSCAN = 5;
const LATENCY_TABLE_COLUMNS =
  "minmax(62px, 1.05fr) repeat(2, minmax(48px, 0.7fr)) repeat(4, minmax(54px, 0.76fr)) minmax(54px, 0.76fr) minmax(58px, 0.82fr)";

function clamp(value: number, min: number, max: number) {
  return Math.min(max, Math.max(min, value));
}

function LatencyVirtualList({ rows }: { rows: LatencyTableSnapshot["rows"] }) {
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
  const effectiveViewportHeight = viewportHeight || LATENCY_ROW_HEIGHT * 10;
  const maxScrollTop = Math.max(0, totalHeight - effectiveViewportHeight);
  const effectiveScrollTop = clamp(scrollTop, 0, maxScrollTop);
  const visibleCapacity = Math.ceil(effectiveViewportHeight / LATENCY_ROW_HEIGHT);
  const poolSize = visibleCapacity + LATENCY_ROW_OVERSCAN * 2;
  const maxStartIndex = Math.max(0, rows.length - poolSize);
  const startIndex = clamp(Math.floor(effectiveScrollTop / LATENCY_ROW_HEIGHT) - LATENCY_ROW_OVERSCAN, 0, maxStartIndex);
  const endIndex = Math.min(rows.length, startIndex + poolSize);
  const visibleRows = rows.slice(startIndex, endIndex);

  return (
    <Box flex="1" minH={0} display="flex" flexDirection="column">
      <Box
        display="grid"
        gridTemplateColumns={LATENCY_TABLE_COLUMNS}
        gap={2}
        px={3}
        h="26px"
        alignItems="center"
        borderBottomWidth="1px"
        borderColor="rgba(92,255,138,0.12)"
        bg="rgba(92,255,138,0.045)"
      >
        {["Button", "STM32", "TX", "IRQ", "Decode", "EPWait", "Submit", "RX", "Total"].map((label, index) => (
          <Text key={label} fontSize="sm" color="gray.500" fontWeight="semibold" textAlign={index === 0 ? "left" : "right"}>
            {label}
          </Text>
        ))}
      </Box>
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
                  key={row.key}
                  h={`${LATENCY_ROW_HEIGHT}px`}
                  px={3}
                  display="grid"
                  gridTemplateColumns={LATENCY_TABLE_COLUMNS}
                  gap={2}
                  alignItems="center"
                  borderBottomWidth="1px"
                  borderColor="rgba(92,255,138,0.08)"
                  bg={index % 2 === 0 ? "rgba(0,0,0,0.12)" : "rgba(92,255,138,0.035)"}
                >
                  <Text fontSize="sm" color="gray.100" minW={0} overflow="hidden" textOverflow="ellipsis" whiteSpace="nowrap">
                    {row.buttonLabel}
                  </Text>
                  <Text fontSize="sm" color="gray.200" textAlign="right">{row.stm32Text}</Text>
                  <Text fontSize="sm" color="gray.200" textAlign="right">{row.txText}</Text>
                  <Text fontSize="sm" color="gray.200" textAlign="right">{row.rxIrqText}</Text>
                  <Text fontSize="sm" color="gray.200" textAlign="right">{row.rxDecodeText}</Text>
                  <Text fontSize="sm" color="gray.200" textAlign="right">{row.rxEpWaitText}</Text>
                  <Text fontSize="sm" color="gray.200" textAlign="right">{row.rxSubmitText}</Text>
                  <Text fontSize="sm" color="gray.200" textAlign="right">{row.rxText}</Text>
                  <Text fontSize="sm" color="green.200" fontWeight="semibold" textAlign="right">{row.totalText}</Text>
                </Box>
              );
            })}
          </Box>
        </Box>
      </Box>
    </Box>
  );
}

export function LatencyTableContentView() {
  const { buttonLatency } = useMonitorStream();
  const [clearAfterMs, setClearAfterMs] = React.useState(() => readCardClearAfter("latency"));
  const rows = React.useMemo(
    () => buttonLatency.items.filter((row) => row.timestampMs >= clearAfterMs),
    [buttonLatency.items, clearAfterMs],
  );
  const status = buttonLatency.status && buttonLatency.status.timestampMs >= clearAfterMs
    ? buttonLatency.status
    : null;
  const table = React.useMemo(
    () => buildLatencyTableSnapshot(rows, status),
    [rows, status],
  );

  React.useEffect(() => subscribeCardClear("latency", setClearAfterMs), []);

  return (
    <Box w="100%" h="100%" minW={0} minH={0} display="flex" flexDirection="column" overflow="hidden">
      <LatencyVirtualList rows={table.rows} />
    </Box>
  );
}
