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
import { LuWifi, LuWifiHigh, LuWifiLow, LuWifiZero } from "react-icons/lu";
import { TfiClose } from "react-icons/tfi";
import type { DebugApplyState, DebugConfig, DebugHidPeriodMs, MonitorEvent, PacketEvent, PowerStatusEvent } from "../../../shared/monitor-types";
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

function latestRssiPacket(packets: Array<PacketEvent & { id?: string }>) {
  for (let i = packets.length - 1; i >= 0; i--) {
    const packet = packets[i];
    if (packet.messageType.startsWith("RFH_RHR1_") && typeof packet.rssiLast === "number") {
      return packet;
    }
  }
  return null;
}

function displayRssiValue(packet: PacketEvent | null) {
  return typeof packet?.rssiLast === "number" ? String(packet.rssiLast) : "--";
}

function displayRssiDetail(packet: PacketEvent | null) {
  return typeof packet?.rssiAvg === "number" ? `avg ${packet.rssiAvg}` : "avg --";
}

function displayMillivolts(mv?: number) {
  return typeof mv === "number" && Number.isFinite(mv) && mv > 0 ? (mv / 1000).toFixed(2) : "--";
}

function primaryBatteryMv(powerStatus: PowerStatusEvent | null) {
  if (!powerStatus) return undefined;
  if (Number.isFinite(powerStatus.batMv) && powerStatus.batMv > 0) {
    return powerStatus.batMv;
  }
  return powerStatus.activeBattery === "H2" ? powerStatus.h2Mv : powerStatus.h1Mv;
}

const VOLTAGE_SAMPLE_INTERVAL_MS = 60 * 60 * 1000;
const MAX_VOLTAGE_HISTORY_POINTS = 240;
const VOLTAGE_AXIS_LABEL_FONT_SIZE = "9px";

type VoltageHistoryPoint = {
  tMs: number;
  mv: number;
};

function formatDurationHours(ms: number) {
  if (!Number.isFinite(ms) || ms <= 0) return "0h";
  const hours = ms / VOLTAGE_SAMPLE_INTERVAL_MS;
  return hours < 10 ? `${hours.toFixed(1)}h` : `${Math.round(hours)}h`;
}

function rssiSignalLevel(packet: PacketEvent | null) {
  const rssi = packet?.rssiLast;
  if (typeof rssi !== "number") return 0;
  if (rssi >= -60) return 3;
  if (rssi >= -70) return 2;
  if (rssi >= -80) return 1;
  return 0;
}

function RssiSignalIcon({ level }: { level: number }) {
  const icons = [LuWifiZero, LuWifiLow, LuWifiHigh, LuWifi] as const;
  const Icon = icons[Math.max(0, Math.min(3, level))];
  return <Icon aria-hidden size={18} />;
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
  detail,
  alert,
  cornerPrefixLabel,
  cornerPrefixValue,
  cornerPrefixDetail,
  cornerPrefixAlert,
  cornerLabel,
  cornerValue,
  cornerDetail,
  cornerSignalLevel,
}: {
  title: string;
  subtitle?: string;
  value: string;
  unit: string;
  status?: string;
  statusLabel?: string;
  target?: string;
  detail?: string;
  alert?: boolean;
  cornerPrefixLabel?: string;
  cornerPrefixValue?: string;
  cornerPrefixDetail?: string;
  cornerPrefixAlert?: boolean;
  cornerLabel?: string;
  cornerValue?: string;
  cornerDetail?: string;
  cornerSignalLevel?: number;
}) {
  return (
    <Card.Root
      variant="outline"
      {...panelSurfaceProps}
      borderColor={alert ? "rgba(255,96,96,0.48)" : panelSurfaceProps.borderColor}
    >
      <Card.Body px={4} py={4} position="relative">
        <Text fontSize="sm" color="gray.400">
          {title}
        </Text>
        {target ? (
          <Text position="absolute" top={4} right={4} fontSize="xs" color="gray.400">
            {target}
          </Text>
        ) : null}
        {status || target ? (
          <HStack mt={2} justify="space-between">
            {status ? <Badge colorPalette={badgeColor(status)}>{statusLabel ?? status}</Badge> : <Box />}
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
        {detail ? (
          <Text mt={2} fontSize="xs" color="gray.400" lineHeight="1.25" maxW="calc(100% - 64px)">
            {detail}
          </Text>
        ) : null}
        {cornerLabel || cornerPrefixLabel ? (
          <HStack position="absolute" right={4} bottom={6} gap={5} align="flex-end">
            {cornerPrefixLabel ? (
              <Box textAlign="right">
                <Text fontSize="10px" color="gray.500" lineHeight="1">
                  {cornerPrefixLabel}
                </Text>
                <Text fontSize="lg" color={cornerPrefixAlert ? "red.200" : neonGreen} lineHeight="1.15">
                  {cornerPrefixValue ?? "--"}
                </Text>
                {cornerPrefixDetail ? (
                  <Text fontSize="10px" color="gray.400" lineHeight="1">
                    {cornerPrefixDetail}
                  </Text>
                ) : null}
              </Box>
            ) : null}
            {cornerLabel ? (
              <Box textAlign="right">
                <Text fontSize="10px" color="gray.500" lineHeight="1">
                  {cornerLabel}
                </Text>
                <HStack gap={1} justify="flex-end" color={neonGreen} lineHeight="1.15">
                  {typeof cornerSignalLevel === "number" ? <RssiSignalIcon level={cornerSignalLevel} /> : null}
                  <Text fontSize="lg">
                    {cornerValue ?? "--"}
                  </Text>
                </HStack>
                {cornerDetail ? (
                  <Text fontSize="10px" color="gray.400" lineHeight="1">
                    {cornerDetail}
                  </Text>
                ) : null}
              </Box>
            ) : null}
          </HStack>
        ) : null}
      </Card.Body>
    </Card.Root>
  );
}

function RateLossMetricCard({
  reportHz,
  packetLoss,
}: {
  reportHz: number;
  packetLoss: number;
}) {
  const lossAlert = packetLoss >= 3;

  const MetricSection = ({
    title,
    value,
    unit,
    description,
    alert,
  }: {
    title: string;
    value: string;
    unit: string;
    description: string;
    alert?: boolean;
  }) => (
    <HStack h="100%" justify="space-between" align="center" gap={3}>
      <Box>
        <Text fontSize="sm" color="gray.400">
          {title}
        </Text>
        <HStack mt={1} gap={1.5} align="baseline">
          <Heading size="lg" color={alert ? "red.200" : "gray.50"} lineHeight="1.05">
            {value}
          </Heading>
          <Text fontSize="sm" color={alert ? "red.200" : neonGreen}>
            {unit}
          </Text>
        </HStack>
      </Box>
      <Text fontSize="xs" color="gray.400" textAlign="right" maxW="170px" lineHeight="1.2">
        {description}
      </Text>
    </HStack>
  );

  return (
    <Card.Root
      variant="outline"
      {...panelSurfaceProps}
      borderColor={lossAlert ? "rgba(255,96,96,0.48)" : panelSurfaceProps.borderColor}
    >
      <Card.Body px={4} py={0} position="relative" h="100%">
        <Box position="absolute" left={4} right={4} top="50%" h="1px" bg="rgba(92,255,138,0.18)" transform="translateY(-0.5px)" />
        <Box h="50%" py={2}>
          <MetricSection
            title="Report Rate"
            value={reportHz.toFixed(1)}
            unit="Hz"
            description="HID telemetry / Monitoring packet rate"
          />
        </Box>
        <Box h="50%" py={2}>
          <MetricSection
            title="RF Packet Loss"
            value={packetLoss.toFixed(2)}
            unit="%"
            description="Recent telemetry window packet loss rate"
            alert={lossAlert}
          />
        </Box>
      </Card.Body>
    </Card.Root>
  );
}

function VoltageTimelineCard({
  points,
}: {
  points: VoltageHistoryPoint[];
}) {
  const chartBoxRef = useRef<HTMLDivElement | null>(null);
  const [chartSize, setChartSize] = useState({ width: 176, height: 72 });

  useEffect(() => {
    const node = chartBoxRef.current;
    if (!node) return;

    const updateSize = () => {
      const rect = node.getBoundingClientRect();
      setChartSize({
        width: Math.max(120, Math.round(rect.width)),
        height: Math.max(56, Math.round(rect.height)),
      });
    };

    updateSize();
    const resizeObserver = new ResizeObserver(updateSize);
    resizeObserver.observe(node);
    return () => resizeObserver.disconnect();
  }, []);

  const chart = useMemo(() => {
    const margin = { left: 12, right: 12, top: 18, bottom: 10 };
    const plotWidth = Math.max(1, chartSize.width - margin.left - margin.right);
    const plotHeight = Math.max(1, chartSize.height - margin.top - margin.bottom);
    const gridY = [
      margin.top,
      margin.top + plotHeight / 2,
      margin.top + plotHeight,
    ];

    if (points.length === 0) {
      return {
        polyline: "",
        dots: [] as Array<{ x: number; y: number }>,
        gridY,
        axisPath: `M${margin.left} ${margin.top} V${margin.top + plotHeight} H${margin.left + plotWidth}`,
        minMv: 0,
        maxMv: 0,
        durationMs: 0,
      };
    }

    const minMv = Math.min(...points.map((point) => point.mv));
    const maxMv = Math.max(...points.map((point) => point.mv));
    const yPad = Math.max(40, Math.round((maxMv - minMv) * 0.12));
    const yMin = Math.max(0, minMv - yPad);
    const yMax = Math.max(yMin + 1, maxMv + yPad);
    const startMs = points[0].tMs;
    const endMs = points[points.length - 1].tMs;
    const spanMs = Math.max(1, endMs - startMs);
    const dots = points.map((point, index) => {
      const x = points.length === 1 ?
        margin.left + plotWidth * 0.5 :
        margin.left + ((point.tMs - startMs) / spanMs) * plotWidth;
      const yNorm = (point.mv - yMin) / (yMax - yMin);
      const y = margin.top + plotHeight - yNorm * plotHeight;
      return { x, y, index };
    });

    return {
      polyline: dots.map((dot) => `${dot.x.toFixed(2)},${dot.y.toFixed(2)}`).join(" "),
      dots,
      gridY,
      axisPath: `M${margin.left} ${margin.top} V${margin.top + plotHeight} H${margin.left + plotWidth}`,
      minMv,
      maxMv,
      durationMs: endMs - startMs,
    };
  }, [chartSize.height, chartSize.width, points]);

  return (
    <Card.Root variant="outline" {...panelSurfaceProps}>
      <Card.Body px={4} py={3}>
        <HStack justify="space-between" align="flex-start">
          <Text fontSize="sm" color="gray.400">
            Voltage Timeline
          </Text>
          <Text fontSize="sm" color="gray.400" textAlign="right" lineHeight="1.2">
            {points.length} samples
          </Text>
        </HStack>

        <Box ref={chartBoxRef} mt={3} h="70px" position="relative">
          <svg viewBox={`0 0 ${chartSize.width} ${chartSize.height}`} width="100%" height="100%" preserveAspectRatio="none">
            {chart.gridY.map((y, index) => (
              <path
                key={`grid-${index}-${y}`}
                d={`M12 ${y.toFixed(2)} H${Math.max(12, chartSize.width - 12)}`}
                stroke={index === 1 ? "rgba(255,255,255,0.08)" : "rgba(255,255,255,0.06)"}
                strokeWidth="0.8"
              />
            ))}
            <path d={chart.axisPath} stroke="rgba(92,255,138,0.2)" strokeWidth="0.8" fill="none" />
            {chart.polyline ? (
              <polyline
                points={chart.polyline}
                fill="none"
                stroke={neonGreen}
                strokeWidth="1.4"
                strokeLinejoin="round"
                strokeLinecap="round"
              />
            ) : null}
            {chart.dots.map((dot, index) => (
              <circle
                key={`${index}-${dot.x}-${dot.y}`}
                cx={dot.x}
                cy={dot.y}
                r="1.9"
                fill={neonGreen}
                stroke="rgba(0,0,0,0.65)"
                strokeWidth="0.8"
              />
            ))}
          </svg>
          {chart.dots.map((dot, index) => (
            <Text
              key={`label-${index}-${dot.x}-${dot.y}`}
              position="absolute"
              left={`${Math.max(24, Math.min(chartSize.width - 24, dot.x))}px`}
              top={`${Math.max(0, dot.y - 14)}px`}
              transform="translateX(-50%)"
              fontSize={VOLTAGE_AXIS_LABEL_FONT_SIZE}
              lineHeight="1"
              color="gray.300"
              textShadow="0 1px 2px rgba(0,0,0,0.95)"
              pointerEvents="none"
              whiteSpace="nowrap"
            >
              {displayMillivolts(points[index]?.mv)}
            </Text>
          ))}
          {points.length === 0 ? (
            <Text position="absolute" inset={0} display="flex" alignItems="center" justifyContent="center" fontSize="xs" color="gray.500">
              No data
            </Text>
          ) : null}
        </Box>

        <HStack justify="space-between" mt={0.5} color="gray.500" fontSize={VOLTAGE_AXIS_LABEL_FONT_SIZE} lineHeight="1">
          <Text>0h</Text>
          <Text>{formatDurationHours(chart.durationMs)}</Text>
        </HStack>
        <HStack justify="space-between" mt={0.5} color="gray.400" fontSize={VOLTAGE_AXIS_LABEL_FONT_SIZE} lineHeight="1">
          <Text>{chart.minMv > 0 ? displayMillivolts(chart.minMv) : "--"}V</Text>
          <Text>{chart.maxMv > 0 ? displayMillivolts(chart.maxMv) : "--"}V</Text>
        </HStack>
      </Card.Body>
    </Card.Root>
  );
}

const defaultDebugConfig: DebugConfig = {
  hidTelemetryEnabled: true,
  hidPeriodMs: 250,
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
}: {
  packets: ReturnType<typeof useMonitorStream>["packets"];
  channelSwitches: ReturnType<typeof useMonitorStream>["channelSwitches"];
  channelScores: ReturnType<typeof useMonitorStream>["channelScores"];
  serialLogClearVersion: number;
  debugConfig: DebugConfig;
  applyDebugConfig: (next: DebugConfig) => void;
  onClearPackets: () => void;
  onClearChannelEvents: () => void;
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
  | "trend"
  | "latency"
  | "packets"
  | "channelEvents";

type DataCardClearMarks = Partial<Record<DataCardKey, number>>;

function after(timestampMs: number, clearAfterMs: number | undefined) {
  return timestampMs >= (clearAfterMs ?? 0);
}

export function App() {
  const { events, packets, latency, buttonLatency, powerStatus, chart, rateSeries, lossSeries, channelSwitches, channelScores, paused, setPaused, clear } = useMonitorStream();
  const scrollRef = useRef<HTMLDivElement | null>(null);
  const [scrollState, setScrollState] = useState({ top: 0, client: 1, scroll: 1 });
  const [serialLogClearVersion, setSerialLogClearVersion] = useState(0);
  const [cardClearMarks, setCardClearMarks] = useState<DataCardClearMarks>({});
  const [debugConfig, setDebugConfig] = useState<DebugConfig>(defaultDebugConfig);
  const [debugStatus, setDebugStatus] = useState<DebugApplyState>("Idle");
  const [voltageHistory, setVoltageHistory] = useState<VoltageHistoryPoint[]>([]);

  const markCardCleared = (key: DataCardKey) => {
    const timestampMs = key === "latency" ? publishCardClear("latency") : Date.now();
    setCardClearMarks((current) => ({ ...current, [key]: timestampMs }));
  };

  const rfStatus = useMemo(() => latestStatus(events, "RF24G"), [events]);
  const rfRssi = useMemo(() => latestRssiPacket(packets.items), [packets.items]);
  const rfConnected = rfStatus?.state === "Connected";
  const rfActualHz = rfStatus?.actualRateHz ?? 0;
  const reportHz = rfConnected
    ? rfActualHz > 0
      ? rfActualHz
      : latency.estimatedHz > 0
        ? latency.estimatedHz
        : Math.max(packets.usbTxPerSec, packets.rfRxPerSec)
    : 0;
  const latestLoss = rfConnected && lossSeries.length > 0 ? lossSeries[lossSeries.length - 1].value : 0;
  const powerValid = Boolean(powerStatus?.valid);
  const batteryMv = primaryBatteryMv(powerStatus);
  const batteryVoltageText = powerValid ? `${displayMillivolts(batteryMv)}V` : "--V";
  const batteryCornerDetail = powerStatus
    ? `BAT ${powerStatus.valid ? `${powerStatus.socPercent.toFixed(0)}%` : "--%"}`
    : "No data";

  useEffect(() => {
    if (!powerValid || typeof batteryMv !== "number" || batteryMv <= 0 || !powerStatus) {
      return;
    }

    setVoltageHistory((current) => {
      const timestampMs = powerStatus.timestampMs;
      const last = current[current.length - 1];
      if (last && timestampMs - last.tMs < VOLTAGE_SAMPLE_INTERVAL_MS) {
        return current;
      }

      const next = current.concat({ tMs: timestampMs, mv: batteryMv });
      return next.length > MAX_VOLTAGE_HISTORY_POINTS ? next.slice(-MAX_VOLTAGE_HISTORY_POINTS) : next;
    });
  }, [batteryMv, powerStatus, powerValid]);

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
  const canScroll = false;
  const thumbHeightPct = canScroll ? Math.max(8, (scrollState.client / scrollState.scroll) * 100) : 100;
  const thumbTopPct = canScroll
    ? (scrollState.top / Math.max(1, scrollState.scroll - scrollState.client)) * (100 - thumbHeightPct)
    : 0;
  const handleClearData = () => {
    clear();
    setCardClearMarks({});
    setVoltageHistory([]);
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
            value={(rfConnected ? packets.rfRxPerSec : 0).toFixed(1)}
            unit="pkt/s"
            cornerPrefixLabel="BAT"
            cornerPrefixValue={batteryVoltageText}
            cornerPrefixDetail={batteryCornerDetail}
            cornerPrefixAlert={Boolean(powerStatus?.lowBattery)}
            cornerLabel="RSSI"
            cornerValue={displayRssiValue(rfRssi)}
            cornerDetail={displayRssiDetail(rfRssi)}
            cornerSignalLevel={rssiSignalLevel(rfRssi)}
          />
          <RateLossMetricCard reportHz={reportHz} packetLoss={latestLoss} />
          <VoltageTimelineCard points={voltageHistory} />
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
            channelScores={channelScores}
            serialLogClearVersion={serialLogClearVersion}
            debugConfig={debugConfig}
            applyDebugConfig={applyDebugConfig}
            onClearPackets={() => markCardCleared("packets")}
            onClearChannelEvents={() => markCardCleared("channelEvents")}
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
