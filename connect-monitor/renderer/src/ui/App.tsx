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
  SegmentGroup,
  Switch,
  Text,
  VStack,
} from "@chakra-ui/react";
import type { CSSProperties } from "react";
import { useEffect, useMemo, useRef, useState } from "react";
import { FaListUl, FaPause, FaPlay, FaRegWindowMaximize, FaRegWindowMinimize, FaRegWindowRestore, FaTerminal } from "react-icons/fa";
import { GrClearOption } from "react-icons/gr";
import { TfiClose } from "react-icons/tfi";
import type { DebugApplyState, DebugConfig, DebugHidPeriodMs, MonitorEvent } from "../../../shared/monitor-types";
import rfMonitorLogo from "../assets/rf-monitor-logo.png";
import { useMonitorStream } from "./useMonitorStream";
import { ButtonLatencyPanel } from "./ButtonLatencyPanel";
import { ButtonsPanel } from "./ButtonsPanel";
import { publishCardClear } from "./cardClear";
import { ChannelPanel } from "./ChannelPanel";
import { ChannelScorePanel } from "./ChannelScorePanel";
import { PacketsPanel } from "./PacketsPanel";
import { RatePanel } from "./RatePanel";
import { SerialLogPanel } from "./SerialLogPanel";
import { ClearDataIconButton } from "./panelActions";
import { neonGreen, panelSurfaceProps, toolbarActionButtonProps } from "./panelStyles";
import { scrollbarStyle } from "./scrollbarStyle";
import { clearSerialLogLines } from "./serialLogStore";

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

function debugBadgeColor(state: DebugApplyState) {
  if (state === "Applied") return "green";
  if (state === "Applying" || state === "Partial") return "yellow";
  if (state === "Failed") return "red";
  return "gray";
}

function AppLogo() {
  return (
    <Image
      src={rfMonitorLogo}
      alt="RF-Monitor"
      h="40px"
      w="auto"
      ml="-12px"
      maxW={{ base: "220px", md: "320px" }}
      objectFit="contain"
      display="block"
      opacity={1}
      filter="contrast(1.18) brightness(1.22)"
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
  statusLabel,
  target,
  alert,
  onClearData,
}: {
  title: string;
  subtitle?: string;
  value: string;
  unit: string;
  status?: string;
  statusLabel?: string;
  target?: string;
  alert?: boolean;
  onClearData?: () => void;
}) {
  return (
    <Card.Root
      variant="outline"
      {...panelSurfaceProps}
      borderColor={alert ? "rgba(255,96,96,0.48)" : panelSurfaceProps.borderColor}
      position="relative"
    >
      {onClearData ? (
        <Box position="absolute" top="8px" right="8px" zIndex={1}>
          <ClearDataIconButton label={`Clear ${title} data`} onClick={onClearData} />
        </Box>
      ) : null}
      <Card.Body px={4} py={4}>
        <Text fontSize="sm" color="gray.400" pr={onClearData ? "34px" : undefined}>
          {title}
        </Text>
        {status || target ? (
          <HStack mt={2} justify="space-between">
            {status ? <Badge colorPalette={badgeColor(status)}>{statusLabel ?? status}</Badge> : <Box />}
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

const defaultDebugConfig: DebugConfig = {
  hidTelemetryEnabled: false,
  hidPeriodMs: 500,
  autoHopEnabled: true,
  manualChannel: null,
};

function DebugSwitch({
  label,
  checked,
  onCheckedChange,
}: {
  label: string;
  checked: boolean;
  onCheckedChange: (checked: boolean) => void;
}) {
  return (
    <Switch.Root
      checked={checked}
      colorPalette="green"
      size="sm"
      display="flex"
      alignItems="center"
      gap={2}
      onCheckedChange={(details) => onCheckedChange(details.checked)}
    >
      <Switch.HiddenInput />
      <Switch.Control borderColor={checked ? "rgba(92,255,138,0.7)" : "rgba(148,163,184,0.3)"}>
        <Switch.Thumb />
      </Switch.Control>
      <Switch.Label fontSize="11px" color={checked ? neonGreen : "gray.300"} whiteSpace="nowrap">
        {label}
      </Switch.Label>
    </Switch.Root>
  );
}

function PeriodSegmentedControl({
  value,
  onChange,
}: {
  value: DebugHidPeriodMs;
  onChange: (period: DebugHidPeriodMs) => void;
}) {
  const periods: DebugHidPeriodMs[] = [100, 250, 500, 1000];

  return (
    <SegmentGroup.Root
      value={String(value)}
      size="xs"
      colorPalette="green"
      onValueChange={(details) => {
        const next = Number(details.value);
        if (next === 100 || next === 250 || next === 500 || next === 1000) {
          onChange(next);
        }
      }}
      display="flex"
      alignItems="center"
      borderWidth="1px"
      borderColor="rgba(92,255,138,0.22)"
      borderRadius="7px"
      bg="rgba(0,0,0,0.22)"
      p="2px"
    >
      <SegmentGroup.Indicator bg="rgba(92,255,138,0.2)" borderColor="rgba(92,255,138,0.48)" />
      {periods.map((period) => (
        <SegmentGroup.Item
          key={period}
          value={String(period)}
          minW={period === 1000 ? "45px" : "36px"}
          h="22px"
          px={2}
          borderRadius="5px"
          cursor="pointer"
          justifyContent="center"
        >
          <SegmentGroup.ItemHiddenInput />
          <SegmentGroup.ItemText fontSize="11px" color={value === period ? neonGreen : "gray.300"}>
            {period}
          </SegmentGroup.ItemText>
        </SegmentGroup.Item>
      ))}
    </SegmentGroup.Root>
  );
}

function DebugControlCard({
  config,
  status,
  paused,
  applyConfig,
  onPauseToggle,
  onClearData,
}: {
  config: DebugConfig;
  status: DebugApplyState;
  paused: boolean;
  applyConfig: (next: DebugConfig) => void;
  onPauseToggle: () => void;
  onClearData: () => void;
}) {
  return (
    <Card.Root
      variant="outline"
      bg="rgba(8,18,22,0.94)"
      borderColor="rgba(92,255,138,0.24)"
      boxShadow="0 0 0 1px rgba(92,255,138,0.05), 0 18px 36px rgba(0,0,0,0.34)"
    >
      <Card.Body px={4} py={3}>
        <HStack justify="space-between" align="center">
          <Text fontSize="sm" color="gray.400">
            Debug Control
          </Text>
          <Badge colorPalette={debugBadgeColor(status)}>{status}</Badge>
        </HStack>
        <VStack align="stretch" gap={2} mt={3}>
          <HStack justify="space-between" gap={2} align="center">
            <DebugSwitch
              label="HID"
              checked={config.hidTelemetryEnabled}
              onCheckedChange={(checked) => applyConfig({ ...config, hidTelemetryEnabled: checked })}
            />
            <PeriodSegmentedControl
              value={config.hidPeriodMs}
              onChange={(period) => applyConfig({ ...config, hidPeriodMs: period })}
            />
            <Button
              {...toolbarActionButtonProps}
              w="134px"
              variant={paused ? "solid" : toolbarActionButtonProps.variant}
              colorPalette={paused ? "yellow" : "green"}
              onClick={onPauseToggle}
            >
              {paused ? <FaPlay /> : <FaPause />}
              {paused ? "Start Listening" : "Pause Listening"}
            </Button>
          </HStack>
          <Box h="1px" my={1.5} bg="rgba(92,255,138,0.16)" />
          <HStack gap={2} align="center">
            <Button
              {...toolbarActionButtonProps}
              w="100%"
              onClick={onClearData}
            >
              <GrClearOption />
              Clear All Data
            </Button>
          </HStack>
        </VStack>
      </Card.Body>
    </Card.Root>
  );
}

function TrafficPanels({
  packets,
  channelSwitches,
  channelScores,
  serialLogClearVersion,
  debugConfig,
  applyDebugConfig,
  onClearPackets,
  onClearChannelEvents,
  onClearChannelScores,
}: {
  packets: ReturnType<typeof useMonitorStream>["packets"];
  channelSwitches: ReturnType<typeof useMonitorStream>["channelSwitches"];
  channelScores: ReturnType<typeof useMonitorStream>["channelScores"];
  serialLogClearVersion: number;
  debugConfig: DebugConfig;
  applyDebugConfig: (next: DebugConfig) => void;
  onClearPackets: () => void;
  onClearChannelEvents: () => void;
  onClearChannelScores: () => void;
}) {
  const [activeTab, setActiveTab] = useState<"traffic" | "log">("traffic");
  const activeTabProps = {
    variant: "solid",
    colorPalette: "green",
    bg: "rgba(92,255,138,0.18)",
    color: neonGreen,
    borderColor: "rgba(92,255,138,0.64)",
    boxShadow: "0 0 14px rgba(92,255,138,0.18)",
  } as const;

  return (
    <Box flex="1" minH={0} display="flex" flexDirection="column" gap="10px">
      <HStack
        role="tablist"
        aria-label="Traffic view"
        gap={2}
        flexShrink={0}
        p="3px"
        alignSelf="flex-start"
        borderWidth="1px"
        borderRadius="8px"
        borderColor="rgba(92,255,138,0.18)"
        bg="rgba(0,0,0,0.28)"
      >
        <Button
          {...toolbarActionButtonProps}
          {...(activeTab === "traffic" ? activeTabProps : {})}
          role="tab"
          aria-selected={activeTab === "traffic"}
          onClick={() => setActiveTab("traffic")}
        >
          <FaListUl />
          Packets / Channel Events / Scores
        </Button>
        <Button
          {...toolbarActionButtonProps}
          {...(activeTab === "log" ? activeTabProps : {})}
          role="tab"
          aria-selected={activeTab === "log"}
          onClick={() => setActiveTab("log")}
        >
          <FaTerminal />
          Log
        </Button>
      </HStack>
      {activeTab === "traffic" ? (
        <Box
          flex="1"
          minH={0}
          display="grid"
          gridTemplateColumns={{ base: "1fr", xl: "minmax(0, 1fr) 800px 250px" }}
          gap="10px"
          alignItems="stretch"
        >
          <PacketsPanel items={packets.items} fillHeight onClearData={onClearPackets} />
          <ChannelPanel items={channelSwitches} fillHeight onClearData={onClearChannelEvents} />
          <ChannelScorePanel
            items={channelScores}
            fillHeight
            autoHopEnabled={debugConfig.autoHopEnabled}
            onClearData={onClearChannelScores}
            onAutoHopChange={(enabled) => {
              const activeChannel = channelScores.find((item) => item.active)?.channel;
              const currentManualChannel = channelScores.some((item) => item.channel === debugConfig.manualChannel)
                ? debugConfig.manualChannel
                : null;
              const fallbackChannel = activeChannel ?? currentManualChannel ?? channelScores[0]?.channel ?? null;
              if (!enabled && fallbackChannel === null) {
                return;
              }
              applyDebugConfig({
                ...debugConfig,
                autoHopEnabled: enabled,
                manualChannel: enabled ? debugConfig.manualChannel : fallbackChannel,
              });
            }}
            onManualChannelSelect={(channel) => applyDebugConfig({
              ...debugConfig,
              autoHopEnabled: false,
              manualChannel: channel,
            })}
          />
        </Box>
      ) : (
        <SerialLogPanel clearVersion={serialLogClearVersion} />
      )}
    </Box>
  );
}

type DataCardKey =
  | "rfConnection"
  | "reportRate"
  | "packetLoss"
  | "trend"
  | "latency"
  | "packets"
  | "channelEvents"
  | "channelScores";

type DataCardClearMarks = Partial<Record<DataCardKey, number>>;

function after(timestampMs: number, clearAfterMs: number | undefined) {
  return timestampMs >= (clearAfterMs ?? 0);
}

export function App() {
  const { events, packets, latency, buttonLatency, chart, rateSeries, lossSeries, channelSwitches, channelScores, paused, setPaused, clear } = useMonitorStream();
  const scrollRef = useRef<HTMLDivElement | null>(null);
  const [scrollState, setScrollState] = useState({ top: 0, client: 1, scroll: 1 });
  const [serialLogClearVersion, setSerialLogClearVersion] = useState(0);
  const [cardClearMarks, setCardClearMarks] = useState<DataCardClearMarks>({});
  const [debugConfig, setDebugConfig] = useState<DebugConfig>(defaultDebugConfig);
  const [debugStatus, setDebugStatus] = useState<DebugApplyState>("Idle");

  const markCardCleared = (key: DataCardKey) => {
    const timestampMs = key === "latency" ? publishCardClear("latency") : Date.now();
    setCardClearMarks((current) => ({ ...current, [key]: timestampMs }));
  };

  const rfConnectionEvents = useMemo(
    () => events.filter((event) => after(event.timestampMs, cardClearMarks.rfConnection)),
    [events, cardClearMarks.rfConnection],
  );
  const rfStatus = useMemo(() => latestStatus(rfConnectionEvents, "RF24G"), [rfConnectionEvents]);
  const rfConnected = rfStatus?.state === "Connected";
  const rfConnectionRate = rfConnected && rfConnectionEvents.length > 0 ? packets.rfRxPerSec : 0;
  const reportRateSeries = useMemo(
    () => rateSeries.filter((point) => after(point.tMs, cardClearMarks.reportRate)),
    [rateSeries, cardClearMarks.reportRate],
  );
  const reportHz = rfConnected && reportRateSeries.length > 0
    ? reportRateSeries[reportRateSeries.length - 1].hz
    : 0;
  const packetLossSeries = useMemo(
    () => lossSeries.filter((point) => after(point.tMs, cardClearMarks.packetLoss)),
    [lossSeries, cardClearMarks.packetLoss],
  );
  const latestLoss = rfConnected && packetLossSeries.length > 0 ? packetLossSeries[packetLossSeries.length - 1].value : 0;
  const trendPackets = useMemo(() => {
    const items = packets.items.filter((packet) => after(packet.timestampMs, cardClearMarks.trend));
    return {
      ...packets,
      items,
      usbTxPerSec: items.length > 0 ? packets.usbTxPerSec : 0,
      rfRxPerSec: items.length > 0 ? packets.rfRxPerSec : 0,
    };
  }, [packets, cardClearMarks.trend]);
  const trendRateSeries = useMemo(
    () => rateSeries.filter((point) => after(point.tMs, cardClearMarks.trend)),
    [rateSeries, cardClearMarks.trend],
  );
  const trendLossSeries = useMemo(
    () => lossSeries.filter((point) => after(point.tMs, cardClearMarks.trend)),
    [lossSeries, cardClearMarks.trend],
  );
  const trendChannelSwitches = useMemo(
    () => channelSwitches.filter((row) => after(row.timestampMs, cardClearMarks.trend)),
    [channelSwitches, cardClearMarks.trend],
  );
  const trendChart = useMemo(() => ({
    rateSeries: chart.rateSeries.filter((point) => after(point.tMs, cardClearMarks.trend)),
    lossSeries: chart.lossSeries.filter((point) => after(point.tMs, cardClearMarks.trend)),
    channelSwitches: chart.channelSwitches.filter((row) => after(row.timestampMs, cardClearMarks.trend)),
  }), [chart, cardClearMarks.trend]);
  const latencyRows = useMemo(
    () => buttonLatency.items.filter((row) => after(row.timestampMs, cardClearMarks.latency)),
    [buttonLatency.items, cardClearMarks.latency],
  );
  const latencyStatus = buttonLatency.status && after(buttonLatency.status.timestampMs, cardClearMarks.latency)
    ? buttonLatency.status
    : null;
  const packetRows = useMemo(
    () => packets.items.filter((packet) => after(packet.timestampMs, cardClearMarks.packets)),
    [packets.items, cardClearMarks.packets],
  );
  const visiblePackets = useMemo(() => ({ ...packets, items: packetRows }), [packets, packetRows]);
  const visibleChannelSwitches = useMemo(
    () => channelSwitches.filter((row) => after(row.timestampMs, cardClearMarks.channelEvents)),
    [channelSwitches, cardClearMarks.channelEvents],
  );
  const visibleChannelScores = useMemo(
    () => channelScores.filter((row) => after(row.updatedAtMs, cardClearMarks.channelScores)),
    [channelScores, cardClearMarks.channelScores],
  );
  const canScroll = false;
  const thumbHeightPct = canScroll ? Math.max(8, (scrollState.client / scrollState.scroll) * 100) : 100;
  const thumbTopPct = canScroll
    ? (scrollState.top / Math.max(1, scrollState.scroll - scrollState.client)) * (100 - thumbHeightPct)
    : 0;
  const handleClearData = () => {
    clear();
    setCardClearMarks({});
    setSerialLogClearVersion((version) => version + 1);
    void clearSerialLogLines()
      .then(() => setSerialLogClearVersion((version) => version + 1))
      .catch(() => {});
  };
  const applyDebugConfig = (next: DebugConfig) => {
    setDebugConfig(next);
    setDebugStatus("Applying");
    window.connectMonitorApi?.setDebugConfig?.(next)
      .then((nextStatus) => setDebugStatus(nextStatus.state))
      .catch(() => setDebugStatus("Failed"));
  };

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
  }, [channelSwitches.length, events.length, packets.items.length]);

  useEffect(() => {
    window.connectMonitorApi?.getDebugConfig?.()
      .then((saved) => setDebugConfig({ ...defaultDebugConfig, ...saved }))
      .catch(() => {});
    window.connectMonitorApi?.getDebugConfigStatus?.()
      .then((nextStatus) => setDebugStatus(nextStatus.state))
      .catch(() => {});
    const timer = window.setInterval(() => {
      window.connectMonitorApi?.getDebugConfigStatus?.()
        .then((nextStatus) => setDebugStatus(nextStatus.state))
        .catch(() => {});
    }, 1000);
    return () => window.clearInterval(timer);
  }, []);

  return (
    <Box
      position="fixed"
      inset={0}
      w="auto"
      h="auto"
      bg="#041012"
      color="gray.50"
      overflow="hidden"
      backgroundImage={`
        radial-gradient(ellipse at 50% 0%, rgba(39, 255, 171, 0.16) 0%, rgba(39, 255, 171, 0) 36%),
        linear-gradient(135deg, #030b11 0%, #05242b 38%, #071b2c 68%, #02070f 100%)
      `}
      backgroundSize="auto, auto"
      _after={{
        content: '""',
        position: "absolute",
        inset: 0,
        pointerEvents: "none",
        zIndex: 0,
        backgroundImage: `
          radial-gradient(ellipse at center, rgba(0,0,0,1) 0%, rgba(0,0,0,95) 10%, rgba(0,0,0,0.9) 30%, rgba(0,0,0,0.8) 80%, rgba(0,0,0,0) 100%)
        `,
      }}
    >
      <Flex
        px={3}
        py={1}
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
        <HStack gap={3} style={noDragRegionStyle}>
          <WindowControls />
        </HStack>
      </Flex>

      <Box
        ref={scrollRef}
        position="relative"
        zIndex={1}
        px={4}
        py={4}
        overflow="hidden"
        h="calc(100vh - 70px)"
        display="flex"
        flexDirection="column"
        minH={0}
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
          gap="10px"
          mb="10px"
          flexShrink={0}
        >
          <DebugControlCard
            config={debugConfig}
            status={debugStatus}
            paused={paused}
            applyConfig={applyDebugConfig}
            onPauseToggle={() => setPaused(!paused)}
            onClearData={handleClearData}
          />
          <MetricCard
            title="RF Connection"
            status={rfStatus?.state ?? "Disconnected"}
            statusLabel={rfStatus?.statusLabel}
            target={`Target ${rfStatus?.targetRateHz ?? 0} Hz`}
            value={rfConnectionRate.toFixed(1)}
            unit="pkt/s"
            onClearData={() => markCardCleared("rfConnection")}
          />
          <MetricCard
            title="Report Rate"
            subtitle="HID telemetry / Monitoring packet rate"
            value={reportHz.toFixed(1)}
            unit="Hz"
            onClearData={() => markCardCleared("reportRate")}
          />
          <MetricCard
            title="RF Packet Loss"
            subtitle="Recent telemetry window packet loss rate"
            value={latestLoss.toFixed(2)}
            unit="%"
            alert={latestLoss >= 3}
            onClearData={() => markCardCleared("packetLoss")}
          />
        </Box>

        <VStack gap="10px" align="stretch" flex="1" minH={0}>
          <Box
            display="grid"
            gridTemplateColumns={{
              base: "1fr",
              xl: "minmax(400px, 1fr) 725px 625px",
            }}
            gap="10px"
            alignItems="stretch"
            h={{ base: "auto", xl: "420px" }}
            flexShrink={0}
          >
            <RatePanel
              packets={trendPackets}
              latency={latency}
              rateSeries={trendRateSeries}
              lossSeries={trendLossSeries}
              channelSwitches={trendChannelSwitches}
              chartRateSeries={trendChart.rateSeries}
              chartLossSeries={trendChart.lossSeries}
              chartChannelSwitches={trendChart.channelSwitches}
              rfStatus={rfStatus}
              chartHeight="100%"
              compact
              onClearData={() => markCardCleared("trend")}
            />
            <ButtonLatencyPanel
              rows={latencyRows}
              status={latencyStatus}
              onClearData={() => markCardCleared("latency")}
            />
            <ButtonsPanel compact />
          </Box>
          <TrafficPanels
            packets={visiblePackets}
            channelSwitches={visibleChannelSwitches}
            channelScores={visibleChannelScores}
            serialLogClearVersion={serialLogClearVersion}
            debugConfig={debugConfig}
            applyDebugConfig={applyDebugConfig}
            onClearPackets={() => markCardCleared("packets")}
            onClearChannelEvents={() => markCardCleared("channelEvents")}
            onClearChannelScores={() => markCardCleared("channelScores")}
          />
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
