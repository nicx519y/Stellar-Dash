import { Box, Text } from "@chakra-ui/react";
import * as React from "react";

import { PanelHeader, panelSurfaceProps } from "./panelStyles";
import { scrollbarStyle } from "./scrollbarStyle";

export type VirtualColumn<T> = {
  key: string;
  header: string;
  width: string;
  align?: "start" | "end";
  render: (item: T, index: number) => React.ReactNode;
};

const ROW_HEIGHT = 48;
const OVERSCAN = 8;

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
  maxHeight?: number;
}) {
  const [scrollTop, setScrollTop] = React.useState(0);
  const viewportHeight = maxHeight;
  const totalHeight = items.length * ROW_HEIGHT;
  const startIndex = Math.max(0, Math.floor(scrollTop / ROW_HEIGHT) - OVERSCAN);
  const visibleCount = Math.ceil(viewportHeight / ROW_HEIGHT) + OVERSCAN * 2;
  const visibleRows = items.slice(startIndex, startIndex + visibleCount);
  const templateColumns = columns.map((column) => column.width).join(" ");

  return (
    <Box borderWidth="1px" borderRadius="md" overflow="hidden" {...panelSurfaceProps}>
      <PanelHeader title={title} meta={countText} action={action} />
      <Box overflowX="auto" css={scrollbarStyle}>
        <Box minW="980px">
          <Box
            display="grid"
            gridTemplateColumns={templateColumns}
            columnGap={3}
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
              >
                {column.header}
              </Text>
            ))}
          </Box>
          <Box
            h={`${maxHeight}px`}
            overflowY="auto"
            position="relative"
            css={scrollbarStyle}
            onScroll={(event) => setScrollTop(event.currentTarget.scrollTop)}
          >
            <Box h={`${totalHeight}px`} position="relative">
              {visibleRows.map((item, offset) => {
                const index = startIndex + offset;
                return (
                  <Box
                    key={rowKey(item, index)}
                    display="grid"
                    gridTemplateColumns={templateColumns}
                    columnGap={3}
                    alignItems="center"
                    px={3}
                    position="absolute"
                    top={`${index * ROW_HEIGHT}px`}
                    left={0}
                    right={0}
                    h={`${ROW_HEIGHT}px`}
                    bg={index % 2 === 0 ? "rgba(92,255,138,0.035)" : "rgba(0,0,0,0.12)"}
                    borderBottomWidth="1px"
                    borderColor="rgba(92,255,138,0.08)"
                  >
                    {columns.map((column) => (
                      <Box
                        key={column.key}
                        color="gray.200"
                        fontSize="sm"
                        textAlign={column.align === "end" ? "end" : "start"}
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
      </Box>
    </Box>
  );
}
