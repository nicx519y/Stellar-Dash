import { HStack, Text } from "@chakra-ui/react";
import type * as React from "react";

export const neonGreen = "#5CFF8A";
export const panelBg = "rgba(5,12,18,0.76)";
export const panelBorder = "rgba(92,255,138,0.18)";

export const panelSurfaceProps = {
  bg: panelBg,
  borderColor: panelBorder,
  boxShadow: "0 0 0 1px rgba(92,255,138,0.04), 0 18px 42px rgba(0,0,0,0.32)",
} as const;

export const toolbarActionIconSize = "10px";

export const toolbarActionButtonIconStyle = {
  "& svg": {
    width: `${toolbarActionIconSize} !important`,
    height: `${toolbarActionIconSize} !important`,
    minWidth: `${toolbarActionIconSize} !important`,
  },
} as const;

export const toolbarActionButtonProps = {
  size: "sm",
  height: "28px",
  fontSize: "xs",
  variant: "outline",
  colorPalette: "green",
  borderColor: "rgba(92,255,138,0.42)",
  css: toolbarActionButtonIconStyle,
} as const;

export function PanelHeader({
  title,
  meta,
  action,
  borderBottom = false,
  compact = false,
}: {
  title: string;
  meta?: React.ReactNode;
  action?: React.ReactNode;
  borderBottom?: boolean;
  compact?: boolean;
}) {
  return (
    <HStack
      justify="space-between"
      px={3}
      py={compact ? 0 : 4}
      h={compact ? "42px" : undefined}
      minH={compact ? "42px" : undefined}
      gap={4}
      position="relative"
      borderBottomWidth={borderBottom ? "1px" : "0"}
      borderColor="rgba(92,255,138,0.12)"
      _before={{
        content: '""',
        position: "absolute",
        left: 0,
        top: compact ? "10px" : "18px",
        bottom: compact ? "10px" : "18px",
        width: "3px",
        bg: neonGreen,
        boxShadow: "0 0 12px rgba(92,255,138,0.85)",
      }}
    >
      <Text
        flex="1"
        minW={0}
        fontSize="sm"
        lineHeight="1.25"
        color="gray.100"
        fontWeight="semibold"
        letterSpacing="0.02em"
      >
        {title}
      </Text>
      {meta || action ? (
        <HStack gap={3} flexShrink={0} h={compact ? "30px" : undefined} align="center">
          {meta ? (
            <Text fontSize="sm" lineHeight="1.25" color="gray.400" whiteSpace="nowrap">
              {meta}
            </Text>
          ) : null}
          {action}
        </HStack>
      ) : null}
    </HStack>
  );
}
