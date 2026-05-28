import {
  Badge,
  Box,
  Button,
  Card,
  Flex,
  Heading,
  HStack,
  IconButton,
  Image,
  Text,
  VStack,
} from "@chakra-ui/react";
import type { CSSProperties } from "react";
import { useEffect, useMemo, useRef, useState } from "react";
import { FaPause, FaPlay, FaRegWindowMaximize, FaRegWindowMinimize, FaRegWindowRestore } from "react-icons/fa";
import { GrClearOption } from "react-icons/gr";
import { TfiClose } from "react-icons/tfi";
import type { MonitorEvent } from "../../../shared/monitor-types";
import rfMonitorLogo from "../assets/rf-monitor-logo.png";
import { useMonitorStream } from "./useMonitorStream";
import { ChannelPanel } from "./ChannelPanel";
import { ErrorsPanel } from "./ErrorsPanel";
import { PacketsPanel } from "./PacketsPanel";
import { RatePanel } from "./RatePanel";
import { neonGreen, panelSurfaceProps, toolbarActionButtonProps } from "./panelStyles";
import { scrollbarStyle } from "./scrollbarStyle";

const appScrollStyle = {
  ...scrollbarStyle,
  scrollbarWidth: "none",
  "&::-webkit-scrollbar": {
    width: "0px",
    height: "0px",
  },
} as const;

const dragRegionStyle = { WebkitAppRegion: "drag" } as CSSProperties;
const noDragRegionStyle = { WebkitAppRegion: "no-drag" } as CSSProperties;

function latestStatus(events: MonitorEvent[], mode: "USB" | "RF24G") {
  for (let i = events.length - 1; i >= 0; i--) {
    const ev = events[i];
    if (ev.kind === "device_status" && ev.mode === mode) {
      return ev;
    }
  }
  return null;
}

function badgeColor(state: string) {
  if (state === "Connected") return "green";
  if (state === "Connecting") return "yellow";
  if (state === "Error") return "red";
  return "gray";
}

function AppLogo() {
  return (
    <Image
      src={rfMonitorLogo}
      alt="RF-Monitor"
      h="36px"
      w="auto"
      ml="-12px"
      maxW={{ base: "220px", md: "320px" }}
      objectFit="contain"
      display="block"
      filter="drop-shadow(0 0 10px rgba(92,255,138,0.22))"
    />
  );
}

function WindowControls() {
  const [maximized, setMaximized] = useState(false);
  const windowControlIconSize = "10px";

  useEffect(() => {
    let unsubscribe: (() => void) | undefined;
    window.connectMonitorApi?.getWindowState?.()
      .then((state) => setMaximized(Boolean(state.maximized)))
      .catch(() => {});
    if (window.connectMonitorApi?.onWindowState) {
      unsubscribe = window.connectMonitorApi.onWindowState((state) => {
        setMaximized(Boolean(state.maximized));
      });
    }
    return () => {
      unsubscribe?.();
    };
  }, []);

  const controlButtonProps = {
    minW: "32px",
    h: "28px",
    px: 0,
    borderRadius: "6px",
    borderWidth: "1px",
    borderColor: "rgba(92,255,138,0.22)",
    bg: "rgba(8,18,16,0.72)",
    color: "rgba(221,255,229,0.9)",
    fontSize: windowControlIconSize,
    lineHeight: 1,
    css: {
      "& svg": {
        width: `${windowControlIconSize} !important`,
        height: `${windowControlIconSize} !important`,
      },
    },
    _hover: {
      bg: "rgba(92,255,138,0.14)",
      borderColor: "rgba(92,255,138,0.54)",
      color: neonGreen,
      boxShadow: "0 0 12px rgba(92,255,138,0.2)",
    },
    _active: {
      bg: "rgba(92,255,138,0.22)",
    },
  } as const;

  return (
    <HStack gap={1} style={noDragRegionStyle}>
      <IconButton
        {...controlButtonProps}
        aria-label="Minimize"
        title="Minimize"
        onClick={() => window.connectMonitorApi?.minimizeWindow?.()}
      >
        <FaRegWindowMinimize />
      </IconButton>
      <IconButton
        {...controlButtonProps}
        aria-label={maximized ? "Restore" : "Maximize"}
        title={maximized ? "Restore" : "Maximize"}
        onClick={() => {
          window.connectMonitorApi?.toggleMaximizeWindow?.()
            .then((nextMaximized) => setMaximized(Boolean(nextMaximized)))
            .catch(() => {});
        }}
      >
        {maximized ? (
          <FaRegWindowRestore />
        ) : (
          <FaRegWindowMaximize />
        )}
      </IconButton>
      <IconButton
        {...controlButtonProps}
        aria-label="Close"
        title="Close"
        onClick={() => window.connectMonitorApi?.closeWindow?.()}
      >
        <TfiClose />
      </IconButton>
    </HStack>
  );
}

function MetricCard({
  title,
  subtitle,
  value,
  unit,
  status,
  target,
  alert,
}: {
  title: string;
  subtitle?: string;
  value: string;
  unit: string;
  status?: string;
  target?: string;
  alert?: boolean;
}) {
  return (
    <Card.Root
      variant="outline"
      {...panelSurfaceProps}
      borderColor={alert ? "rgba(255,96,96,0.48)" : panelSurfaceProps.borderColor}
    >
      <Card.Body px={4} py={4}>
        <Text fontSize="sm" color="gray.400">
          {title}
        </Text>
        {status || target ? (
          <HStack mt={2} justify="space-between">
            {status ? <Badge colorPalette={badgeColor(status)}>{status}</Badge> : <Box />}
            {target ? (
              <Text fontSize="sm" color="gray.400">
                {target}
              </Text>
            ) : null}
          </HStack>
        ) : (
          <Text fontSize="sm" color="gray.400" mt={2}>
            {subtitle}
          </Text>
        )}
        <Heading size="lg" mt={3} color={alert ? "red.200" : "gray.50"} textShadow={alert ? undefined : "0 0 16px rgba(92,255,138,0.22)"}>
          {value}
        </Heading>
        <Text fontSize="sm" color={alert ? "red.200" : neonGreen}>
          {unit}
        </Text>
      </Card.Body>
    </Card.Root>
  );
}

function InfoTabs({
  packets,
  channelSwitches,
  errors,
}: {
  packets: ReturnType<typeof useMonitorStream>["packets"];
  channelSwitches: ReturnType<typeof useMonitorStream>["channelSwitches"];
  errors: ReturnType<typeof useMonitorStream>["errors"];
}) {
  const [activeTab, setActiveTab] = useState<"traffic" | "errors">("traffic");
  const triggerProps = {
    px: 4,
    h: "34px",
    borderRadius: "6px",
    color: "gray.300",
    fontSize: "sm",
    fontWeight: "semibold",
    _hover: {
      color: "gray.50",
      bg: "rgba(92,255,138,0.08)",
    },
  } as const;

  return (
    <Box>
      <HStack
        gap={2}
        mb={3}
        p={1}
        w="fit-content"
        borderWidth="1px"
        borderRadius="8px"
        borderColor="rgba(92,255,138,0.18)"
        bg="rgba(5,12,18,0.76)"
      >
        <Button
          {...triggerProps}
          variant="ghost"
          color={activeTab === "traffic" ? neonGreen : triggerProps.color}
          bg={activeTab === "traffic" ? "rgba(92,255,138,0.12)" : "transparent"}
          boxShadow={activeTab === "traffic" ? "inset 0 0 0 1px rgba(92,255,138,0.28), 0 0 12px rgba(92,255,138,0.16)" : undefined}
          onClick={() => setActiveTab("traffic")}
        >
          Packets & Channel Events
        </Button>
        <Button
          {...triggerProps}
          variant="ghost"
          color={activeTab === "errors" ? neonGreen : triggerProps.color}
          bg={activeTab === "errors" ? "rgba(92,255,138,0.12)" : "transparent"}
          boxShadow={activeTab === "errors" ? "inset 0 0 0 1px rgba(92,255,138,0.28), 0 0 12px rgba(92,255,138,0.16)" : undefined}
          onClick={() => setActiveTab("errors")}
        >
          Errors
        </Button>
      </HStack>
      {activeTab === "traffic" ? (
        <Box display="grid" gridTemplateColumns={{ base: "1fr", xl: "1fr 1fr" }} gap={4} alignItems="start">
          <PacketsPanel items={packets.items} />
          <ChannelPanel items={channelSwitches} />
        </Box>
      ) : (
        <ErrorsPanel items={errors.items} />
      )}
    </Box>
  );
}

export function App() {
  const { events, packets, errors, latency, rateSeries, lossSeries, channelSwitches, paused, setPaused, clear } = useMonitorStream();
  const scrollRef = useRef<HTMLDivElement | null>(null);
  const [scrollState, setScrollState] = useState({ top: 0, client: 1, scroll: 1 });

  const usbStatus = useMemo(() => latestStatus(events, "USB"), [events]);
  const rfStatus = useMemo(() => latestStatus(events, "RF24G"), [events]);
  const rfActualHz = rfStatus?.actualRateHz ?? 0;
  const reportHz = rfActualHz > 0 ? rfActualHz : latency.estimatedHz > 0 ? latency.estimatedHz : Math.max(packets.usbTxPerSec, packets.rfRxPerSec);
  const latestLoss = lossSeries.length > 0 ? lossSeries[lossSeries.length - 1].value : 0;
  const canScroll = scrollState.scroll > scrollState.client + 1;
  const thumbHeightPct = canScroll ? Math.max(8, (scrollState.client / scrollState.scroll) * 100) : 100;
  const thumbTopPct = canScroll
    ? (scrollState.top / Math.max(1, scrollState.scroll - scrollState.client)) * (100 - thumbHeightPct)
    : 0;

  useEffect(() => {
    const scroller = scrollRef.current;
    if (!scroller) return;

    const updateScrollState = () => {
      setScrollState({
        top: scroller.scrollTop,
        client: scroller.clientHeight,
        scroll: scroller.scrollHeight,
      });
    };
    updateScrollState();

    const resizeObserver = new ResizeObserver(updateScrollState);
    resizeObserver.observe(scroller);
    const firstChild = scroller.firstElementChild;
    if (firstChild) {
      resizeObserver.observe(firstChild);
    }
    window.addEventListener("resize", updateScrollState);
    return () => {
      resizeObserver.disconnect();
      window.removeEventListener("resize", updateScrollState);
    };
  }, [channelSwitches.length, errors.items.length, events.length, packets.items.length]);

  return (
    <Box
      position="fixed"
      inset={0}
      w="auto"
      h="auto"
      bg="#05080d"
      color="gray.50"
      overflow="hidden"
      backgroundImage={`
        linear-gradient(rgba(92,255,138,0.08) 1px, transparent 1px),
        linear-gradient(90deg, rgba(92,255,138,0.07) 1px, transparent 1px),
        repeating-linear-gradient(135deg, rgba(98,247,255,0.06) 0 1px, transparent 1px 18px),
        linear-gradient(180deg, #05080d 0%, #0a1118 54%, #05080d 100%)
      `}
      backgroundSize="44px 44px, 44px 44px, 18px 18px, auto"
      _before={{
        content: '""',
        position: "absolute",
        inset: 0,
        pointerEvents: "none",
        backgroundImage: "linear-gradient(90deg, transparent 0 72px, rgba(92,255,138,0.055) 72px 74px, transparent 74px 112px)",
        opacity: 0.72,
      }}
    >
      <Flex
        px={3}
        py={2}
        align="center"
        justify="space-between"
        position="relative"
        zIndex={1}
        borderBottomWidth="1px"
        borderColor="rgba(92,255,138,0.18)"
        bg="rgba(0,0,0,0.42)"
        boxShadow="0 12px 36px rgba(0,0,0,0.28)"
        style={dragRegionStyle}
      >
        <AppLogo />
        <HStack gap={4} style={noDragRegionStyle}>
          <HStack gap={3}>
            <Button
              {...toolbarActionButtonProps}
              variant={paused ? "solid" : toolbarActionButtonProps.variant}
              colorPalette={paused ? "yellow" : "green"}
              onClick={() => setPaused(!paused)}
            >
              {paused ? <FaPlay /> : <FaPause />}
              {paused ? "Start Listening" : "Pause Listening"}
            </Button>
            <Button
              {...toolbarActionButtonProps}
              onClick={clear}
            >
              <GrClearOption />
              Clear Data
            </Button>
          </HStack>
          <WindowControls />
        </HStack>
      </Flex>

      <Box
        ref={scrollRef}
        px={4}
        py={4}
        overflow="auto"
        h="calc(100vh - 76px)"
        position="relative"
        zIndex={1}
        onScroll={(event) => {
          const scroller = event.currentTarget;
          setScrollState({
            top: scroller.scrollTop,
            client: scroller.clientHeight,
            scroll: scroller.scrollHeight,
          });
        }}
        css={appScrollStyle}
      >
        <Box
          display="grid"
          gridTemplateColumns={{ base: "1fr", md: "1fr 1fr", lg: "repeat(4, 1fr)" }}
          gap={3}
          mb={4}
        >
          <MetricCard
            title="USB Connection"
            status={usbStatus?.state ?? "Disconnected"}
            target={`Target ${usbStatus?.targetRateHz ?? 0} Hz`}
            value={packets.usbTxPerSec.toFixed(1)}
            unit="pkt/s"
          />
          <MetricCard
            title="RF Connection"
            status={rfStatus?.state ?? "Disconnected"}
            target={`Target ${rfStatus?.targetRateHz ?? 0} Hz`}
            value={packets.rfRxPerSec.toFixed(1)}
            unit="pkt/s"
          />
          <MetricCard
            title="Report Rate"
            subtitle="HID telemetry / Monitoring packet rate"
            value={reportHz.toFixed(1)}
            unit="Hz"
          />
          <MetricCard
            title="RF Packet Loss"
            subtitle="Recent telemetry window packet loss rate"
            value={latestLoss.toFixed(2)}
            unit="%"
            alert={latestLoss >= 3}
          />
        </Box>

        <VStack gap={4} align="stretch">
          <RatePanel
            packets={packets}
            latency={latency}
            rateSeries={rateSeries}
            lossSeries={lossSeries}
            channelSwitches={channelSwitches}
            rfStatus={rfStatus}
          />
          <InfoTabs packets={packets} channelSwitches={channelSwitches} errors={errors} />
        </VStack>
      </Box>
      {canScroll ? (
        <Box
          position="absolute"
          top="86px"
          right="6px"
          bottom="8px"
          zIndex={3}
          w="8px"
          borderRadius="999px"
          bg="rgba(92,255,138,0.08)"
          borderWidth="1px"
          borderColor="rgba(92,255,138,0.16)"
          pointerEvents="none"
        >
          <Box
            position="absolute"
            left="1px"
            right="1px"
            top={`${thumbTopPct}%`}
            h={`${thumbHeightPct}%`}
            minH="48px"
            borderRadius="999px"
            bg="linear-gradient(180deg, rgba(92,255,138,0.96), rgba(98,247,255,0.72))"
            boxShadow="0 0 12px rgba(92,255,138,0.7)"
          />
        </Box>
      ) : null}
    </Box>
  );
}
