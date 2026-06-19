import { Box, IconButton, Text } from "@chakra-ui/react";
import * as React from "react";
import { GoMoveToTop } from "react-icons/go";

import { neonGreen, PanelHeader, panelSurfaceProps } from "./panelStyles";
import { scrollbarStyle } from "./scrollbarStyle";

export type VirtualColumn<T> = {
  key: string;
  header: string;
  width: string;
  align?: "start" | "end";
  render: (item: T, index: number) => React.ReactNode;
};

const ROW_HEIGHT = 48;
const OVERSCAN = 6;
const ROW_BG_A = "rgba(92,255,138,0.035)";
const ROW_BG_B = "rgba(0,0,0,0.12)";
const tableScrollStyle = {
  ...scrollbarStyle,
  "@keyframes rfMonitorRowMove": {
    "0%": {
      transform: "translateY(var(--row-shift))",
    },
    "100%": {
      transform: "translateY(0)",
    },
  },
} as const;

function clamp(value: number, min: number, max: number) {
  return Math.min(max, Math.max(min, value));
}

export function VirtualTable<T>({
  title,
  countText,
  items,
  columns,
  rowKey,
  action,
  maxHeight = 530,
}: {
  title: string;
  countText: string;
  items: T[];
  columns: Array<VirtualColumn<T>>;
  rowKey: (item: T, index: number) => string;
  action?: React.ReactNode;
  maxHeight?: number | string;
}) {
  const scrollRef = React.useRef<HTMLDivElement | null>(null);
  const scrollTopRef = React.useRef(0);
  const firstRowKeyRef = React.useRef<string | null>(null);
  const pinnedToTopRef = React.useRef(true);
  const previousPositionsRef = React.useRef<Map<string, number>>(new Map());
  const rowStripeRef = React.useRef<Map<string, 0 | 1>>(new Map());
  const [scrollTop, setScrollTopState] = React.useState(0);
  const [showBackToTop, setShowBackToTop] = React.useState(false);
  const templateColumns = columns.map((column) => column.width).join(" ");
  const totalHeight = items.length * ROW_HEIGHT;
  const measuredMaxHeight = typeof maxHeight === "number" ? maxHeight : 530;
  const visibleCapacity = Math.ceil(measuredMaxHeight / ROW_HEIGHT);
  const poolSize = visibleCapacity + OVERSCAN * 2;
  const maxStartIndex = Math.max(0, items.length - poolSize);
  const startIndex = clamp(Math.floor(scrollTop / ROW_HEIGHT) - OVERSCAN, 0, maxStartIndex);
  const topPadding = startIndex * ROW_HEIGHT;
  const itemKeys = items.map((item, index) => rowKey(item, index));
  const rowStripeMap = rowStripeRef.current;
  const previousFirstKeyForStripe = firstRowKeyRef.current;
  const previousFirstIndexForStripe = previousFirstKeyForStripe ? itemKeys.indexOf(previousFirstKeyForStripe) : -1;

  if (items.length === 0) {
    rowStripeMap.clear();
  } else if (rowStripeMap.size === 0 || previousFirstIndexForStripe < 0) {
    rowStripeMap.clear();
    itemKeys.forEach((key, index) => {
      rowStripeMap.set(key, (index % 2) as 0 | 1);
    });
  } else {
    const anchorKey = previousFirstKeyForStripe ?? itemKeys[previousFirstIndexForStripe];
    const anchorStripe = rowStripeMap.get(anchorKey) ?? ((previousFirstIndexForStripe % 2) as 0 | 1);
    for (let index = previousFirstIndexForStripe - 1; index >= 0; index--) {
      const distance = previousFirstIndexForStripe - index;
      rowStripeMap.set(itemKeys[index], ((anchorStripe + distance) % 2) as 0 | 1);
    }
    for (let index = previousFirstIndexForStripe + 1; index < itemKeys.length; index++) {
      if (!rowStripeMap.has(itemKeys[index])) {
        const distance = index - previousFirstIndexForStripe;
        rowStripeMap.set(itemKeys[index], ((anchorStripe + distance) % 2) as 0 | 1);
      }
    }
    const currentKeys = new Set(itemKeys);
    for (const key of rowStripeMap.keys()) {
      if (!currentKeys.has(key)) {
        rowStripeMap.delete(key);
      }
    }
  }

  const poolRows = Array.from({ length: poolSize }, (_unused, slot) => {
    const index = startIndex + slot;
    const item = items[index];
    return {
      item,
      index,
      slot,
      itemKey: item ? itemKeys[index] : `empty-${slot}`,
    };
  });
  const previousPositions = previousPositionsRef.current;
  const shouldAnimateRows = pinnedToTopRef.current && previousPositions.size > 0;

  const setScrollTop = React.useCallback((nextScrollTop: number) => {
    scrollTopRef.current = nextScrollTop;
    const isPinnedToTop = nextScrollTop <= 1;
    pinnedToTopRef.current = isPinnedToTop;
    setShowBackToTop(!isPinnedToTop);
    setScrollTopState(nextScrollTop);
  }, []);

  const scrollToTop = React.useCallback(() => {
    const scroller = scrollRef.current;
    if (!scroller) return;
    scroller.scrollTop = 0;
    setScrollTop(0);
  }, [setScrollTop]);

  React.useLayoutEffect(() => {
    const currentFirstKey = items.length > 0 ? rowKey(items[0], 0) : null;
    const previousFirstKey = firstRowKeyRef.current;
    const scroller = scrollRef.current;

    if (scroller && previousFirstKey && currentFirstKey && currentFirstKey !== previousFirstKey) {
      const previousFirstIndex = items.findIndex((item, index) => rowKey(item, index) === previousFirstKey);
      if (previousFirstIndex > 0) {
        const insertedHeight = previousFirstIndex * ROW_HEIGHT;
        if (pinnedToTopRef.current) {
          scroller.scrollTop = 0;
          setScrollTop(0);
        } else {
          const preservedScrollTop = scrollTopRef.current + insertedHeight;
          scroller.scrollTop = preservedScrollTop;
          setScrollTop(preservedScrollTop);
        }
      }
    }

    firstRowKeyRef.current = currentFirstKey;
    previousPositionsRef.current = new Map(items.map((item, index) => [rowKey(item, index), index * ROW_HEIGHT]));
  }, [items, rowKey, setScrollTop]);

  return (
    <Box
      borderWidth="1px"
      borderRadius="md"
      overflow="hidden"
      w="100%"
      flex="1"
      h={typeof maxHeight === "number" ? undefined : "100%"}
      display="flex"
      flexDirection="column"
      minH={0}
      {...panelSurfaceProps}
    >
      <PanelHeader title={title} meta={countText} action={action} borderBottom compact />
      <Box overflowX="auto" css={scrollbarStyle} flex="1" minH={0}>
        <Box w="100%" minW="0" h={typeof maxHeight === "number" ? undefined : "100%"} display="flex" flexDirection="column">
          <Box
            display="grid"
            gridTemplateColumns={templateColumns}
            columnGap={0}
            px={3}
            py={2}
            bg="rgba(0,0,0,0.34)"
            borderTopWidth="1px"
            borderBottomWidth="1px"
            borderColor="rgba(92,255,138,0.12)"
            position="sticky"
            top={0}
            zIndex={1}
          >
            {columns.map((column) => (
              <Text
                key={column.key}
                fontSize="sm"
                color="gray.300"
                fontWeight="semibold"
                textAlign={column.align === "end" ? "end" : "start"}
                minW={0}
                overflow="hidden"
                textOverflow="ellipsis"
                whiteSpace="nowrap"
              >
                {column.header}
              </Text>
            ))}
          </Box>
          <Box position="relative" flex="1" minH={0}>
            <Box
              ref={scrollRef}
              h={typeof maxHeight === "number" ? `${maxHeight}px` : maxHeight}
              overflowY="auto"
              position="relative"
              css={tableScrollStyle}
              onScroll={(event) => setScrollTop(event.currentTarget.scrollTop)}
            >
              <Box h={`${totalHeight}px`} minH={typeof maxHeight === "number" ? `${maxHeight}px` : "100%"} position="relative">
                <Box position="absolute" top={`${topPadding}px`} left={0} right={0}>
                  {poolRows.map(({ item, index, slot, itemKey }) => {
                    if (!item) {
                      return <Box key={`pool-${slot}`} h={`${ROW_HEIGHT}px`} display="none" />;
                    }

                    const previousTop = previousPositions.get(itemKey);
                    const rowShift = shouldAnimateRows
                      ? typeof previousTop === "number"
                        ? previousTop - index * ROW_HEIGHT
                        : index === 0
                          ? -ROW_HEIGHT
                          : 0
                      : 0;
                    const rowStyle = rowShift !== 0
                      ? ({ "--row-shift": `${rowShift}px` } as React.CSSProperties)
                      : undefined;

                    return (
                      <Box
                        key={`pool-${slot}`}
                        data-row-key={itemKey}
                        style={rowStyle}
                        display="grid"
                        gridTemplateColumns={templateColumns}
                        columnGap={0}
                        alignItems="center"
                        px={3}
                        h={`${ROW_HEIGHT}px`}
                        bg={(rowStripeMap.get(itemKey) ?? 0) === 0 ? ROW_BG_A : ROW_BG_B}
                        borderBottomWidth="1px"
                        borderColor="rgba(92,255,138,0.08)"
                        animation={rowShift !== 0 ? "rfMonitorRowMove 180ms ease-out" : undefined}
                      >
                        {columns.map((column) => (
                          <Box
                            key={column.key}
                            color="gray.200"
                            fontSize="sm"
                            textAlign={column.align === "end" ? "end" : "start"}
                            minW={0}
                            overflow="hidden"
                            textOverflow="ellipsis"
                            whiteSpace="nowrap"
                          >
                            {column.render(item, index)}
                          </Box>
                        ))}
                      </Box>
                    );
                  })}
                </Box>
              </Box>
            </Box>
            {showBackToTop ? (
              <IconButton
                aria-label="Back to top"
                title="Back to top"
                position="absolute"
                right="12px"
                bottom="12px"
                zIndex={2}
                size="sm"
                minW="32px"
                h="32px"
                borderRadius="999px"
                borderWidth="1px"
                borderColor="rgba(92,255,138,0.42)"
                bg="rgba(8,18,16,0.88)"
                color="rgba(221,255,229,0.92)"
                boxShadow="0 0 18px rgba(0,0,0,0.36)"
                css={{
                  "& svg": {
                    width: "14px !important",
                    height: "14px !important",
                  },
                }}
                _hover={{
                  bg: "rgba(92,255,138,0.16)",
                  color: neonGreen,
                  boxShadow: "0 0 16px rgba(92,255,138,0.26)",
                }}
                _active={{
                  bg: "rgba(92,255,138,0.24)",
                }}
                onClick={scrollToTop}
              >
                <GoMoveToTop />
              </IconButton>
            ) : null}
          </Box>
        </Box>
      </Box>
    </Box>
  );
}
