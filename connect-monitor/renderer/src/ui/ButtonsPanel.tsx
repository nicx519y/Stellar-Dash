import { Badge, Box, Card } from "@chakra-ui/react";
import { useCallback, useEffect, useLayoutEffect, useRef, useState } from "react";

import type { HitboxBounds, HitboxSummary } from "../../../shared/monitor-types";
import { PanelHeader, panelSurfaceProps } from "./panelStyles";

const disconnectedSummary: HitboxSummary = {
  connected: false,
  deviceId: null,
  pressedCount: 0,
  timestampMs: 0,
};

function hiddenBounds(compact: boolean): HitboxBounds {
  return {
    x: 0,
    y: 0,
    width: 1,
    height: 1,
    visible: false,
    compact,
  };
}

function HitboxViewSlot({ compact }: { compact: boolean }) {
  const slotRef = useRef<HTMLDivElement | null>(null);
  const frameRef = useRef<number | null>(null);

  const syncBounds = useCallback(() => {
    if (frameRef.current !== null) return;
    frameRef.current = window.requestAnimationFrame(() => {
      frameRef.current = null;
      const slot = slotRef.current;
      if (!slot) {
        window.connectMonitorApi?.setHitboxBounds?.(hiddenBounds(compact));
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

      window.connectMonitorApi?.setHitboxBounds?.({
        x: rect.left,
        y: rect.top,
        width: rect.width,
        height: rect.height,
        visible,
        compact,
      });
    });
  }, [compact]);

  useLayoutEffect(() => {
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
      window.connectMonitorApi?.setHitboxBounds?.(hiddenBounds(compact));
    };
  }, [compact, syncBounds]);

  return (
    <Box
      ref={slotRef}
      w="100%"
      h="100%"
      minH={0}
      flex="1"
      position="relative"
      overflow="hidden"
      borderRadius="6px"
    />
  );
}

export function ButtonsPanel({ compact = false }: { compact?: boolean }) {
  const [summary, setSummary] = useState<HitboxSummary>(disconnectedSummary);
  const sourceLabel = summary.connected ? "XInput Connected" : "Disconnected";
  const meta = `${summary.pressedCount} pressed`;

  useEffect(() => {
    return window.connectMonitorApi?.onHitboxSummary?.((nextSummary) => {
      setSummary(nextSummary);
    });
  }, []);

  return (
    <Card.Root variant="outline" overflow="hidden" h="100%" {...panelSurfaceProps}>
      <PanelHeader
        title="Gamepad Buttons"
        meta={meta}
        action={
          <Badge colorPalette={summary.connected ? "green" : "gray"}>
            {sourceLabel}
          </Badge>
        }
        borderBottom
        compact={compact}
      />
      <Card.Body
        px={compact ? 3 : { base: 3, md: 5 }}
        py={compact ? 3 : { base: 4, md: 5 }}
        display="flex"
        flexDirection="column"
        flex="1"
        minH={0}
      >
        <HitboxViewSlot compact={compact} />
      </Card.Body>
    </Card.Root>
  );
}
