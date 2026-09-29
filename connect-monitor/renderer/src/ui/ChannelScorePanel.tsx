import { Badge, Box, Button, HStack, Switch, Text, VStack } from "@chakra-ui/react";
import { useEffect, useMemo, useRef, useState } from "react";

import type { DebugConfig, DebugConfigStatus, PacketEvent } from "../../../shared/monitor-types";
import type { ChannelScoreRow } from "./useMonitorStream";
import { neonGreen, PanelHeader, panelSurfaceProps } from "./panelStyles";
import { hiddenScrollbarStyle } from "./scrollbarStyle";

type SwitchRequest = { target: number | null; startedAt: number; mode: "manual" | "auto" };
type ChannelSnapshot = {
  current: number | null;
  connected: boolean;
  inputAt: number;
  txPrimary: number | null;
  txAt: number;
  rxChannel: number | null;
  rxAt: number;
};

function snapshot(packets: PacketEvent[], now: number): ChannelSnapshot {
  let input: PacketEvent | undefined;
  let tx: PacketEvent | undefined;
  let rx: PacketEvent | undefined;
  for (let i = packets.length - 1; i >= 0; i--) {
    const packet = packets[i];
    if (!input && packet.messageType.startsWith("RFH_RHM1_")) input = packet;
    if (!tx && packet.rfChannel?.page === 0) tx = packet;
    if (!rx && packet.rfChannel?.page === 4) rx = packet;
    if (input && tx && rx) break;
  }
  const inputFresh = !!input && now - input.timestampMs < 1500;
  const txFresh = !!tx && now - tx.timestampMs < 3500 && (tx.rfChannel?.ageMs ?? 65535) < 3500;
  const rxFresh = !!rx && now - rx.timestampMs < 3500 && (rx.rfChannel?.ageMs ?? 65535) < 3500;
  return {
    current: inputFresh ? input?.channelNumber ?? null : null,
    connected: inputFresh && input?.rfStateCode === "C" && (input.sampleCount ?? 0) > 0,
    inputAt: inputFresh ? input!.timestampMs : 0,
    txPrimary: txFresh ? tx?.rfChannel?.primary ?? null : null,
    txAt: txFresh ? tx!.timestampMs : 0,
    rxChannel: rxFresh ? rx?.rfChannel?.receiverChannel ?? null : null,
    rxAt: rxFresh ? rx!.timestampMs : 0,
  };
}

function scoreColor(score: number) {
  if (score === 65535) return "gray";
  if (score <= 120) return "green";
  if (score <= 400) return "yellow";
  return "red";
}

export function ChannelScorePanel({
  items,
  packets,
  config,
  paused,
  applyConfig,
  fillHeight = false,
}: {
  items: ChannelScoreRow[];
  packets: PacketEvent[];
  config: DebugConfig;
  paused: boolean;
  applyConfig: (next: DebugConfig) => Promise<DebugConfigStatus | null>;
  fillHeight?: boolean;
}) {
  const [now, setNow] = useState(Date.now());
  const [submitting, setSubmitting] = useState(false);
  const [request, setRequest] = useState<SwitchRequest | null>(null);
  const [result, setResult] = useState("");
  const [resultTone, setResultTone] = useState<"gray" | "yellow" | "green" | "red">("gray");
  const state = useMemo(() => snapshot(packets, now), [packets, now]);
  const stateRef = useRef(state);
  stateRef.current = state;
  const configRef = useRef(config);
  configRef.current = config;

  useEffect(() => {
    const timer = window.setInterval(() => setNow(Date.now()), 500);
    return () => window.clearInterval(timer);
  }, []);

  useEffect(() => {
    if (!request) return;
    let active = true;
    let polling = false;
    const poll = async () => {
      if (polling || !active) return;
      polling = true;
      try {
        const status = await window.connectMonitorApi?.getDebugConfigStatus?.();
        if (!active) return;
        const currentConfig = configRef.current;
        if (currentConfig.autoHopEnabled !== (request.mode === "auto") ||
            (request.mode === "manual" && currentConfig.manualChannel !== request.target)) {
          setResult("频道设置已被其他操作替换");
          setResultTone("yellow");
          setRequest(null);
          return;
        }
        if (status?.state === "Failed") {
          setResult(status.message ?? "频道配置失败");
          setResultTone("red");
          setRequest(null);
          return;
        }
        const current = stateRef.current;
        const inputRestored = current.connected && current.inputAt > request.startedAt;
        const channelCommitted = request.mode === "auto" || (
          current.current === request.target && current.txPrimary === request.target &&
          current.rxChannel === request.target && current.txAt > request.startedAt &&
          current.rxAt > request.startedAt
        );
        if (status?.state === "Applied" && channelCommitted && inputRestored) {
          setResult(request.mode === "auto" ? "自动选频已开启" : `CH ${request.target} 已生效，输入正常`);
          setResultTone("green");
          setRequest(null);
          return;
        }
        if (Date.now() - request.startedAt >= 6500) {
          setResult(status?.state !== "Applied" ? "双方配置未生效；请查看 Channel Events" :
            "未确认频道或输入恢复；请查看 Channel Events");
          setResultTone("red");
          setRequest(null);
        }
      } catch {
        if (active) {
          setResult("无法读取频道配置状态");
          setResultTone("red");
          setRequest(null);
        }
      } finally {
        polling = false;
      }
    };
    const timer = window.setInterval(() => void poll(), 500);
    void poll();
    return () => { active = false; window.clearInterval(timer); };
  }, [request]);

  const canOperate = !paused && config.hidTelemetryEnabled && !submitting && !request;
  const currentInList = state.current !== null && items.some((item) => item.channel === state.current);
  const change = async (mode: "manual" | "auto", target: number | null) => {
    if (!canOperate) return;
    const startedAt = Date.now();
    setSubmitting(true);
    setResult(mode === "auto" ? "正在开启自动选频…" : `正在设置 CH ${target}…`);
    setResultTone("yellow");
    try {
      const status = await applyConfig({ ...config, autoHopEnabled: mode === "auto", manualChannel: target ?? config.manualChannel });
      if (!status || status.state === "Failed") {
        setResult(status?.message ?? "无法提交频道配置");
        setResultTone("red");
      } else {
        setRequest({ mode, target, startedAt });
      }
    } catch {
      setResult("无法提交频道配置");
      setResultTone("red");
    } finally {
      setSubmitting(false);
    }
  };

  const isV3 = packets.some((packet) => packet.rfChannel !== undefined);
  const activeBorder = config.autoHopEnabled ? "rgba(92,255,138,0.58)" : "rgba(96,165,250,0.66)";
  const activeBg = config.autoHopEnabled ? "rgba(92,255,138,0.11)" : "rgba(96,165,250,0.14)";
  const activeShadow = config.autoHopEnabled ? "0 0 14px rgba(92,255,138,0.16)" : "0 0 16px rgba(96,165,250,0.22)";
  const activePalette = config.autoHopEnabled ? "green" : "blue";

  return (
    <Box borderWidth="1px" borderRadius="md" overflow="hidden" h={fillHeight ? "100%" : undefined}
      display="flex" flexDirection="column" minH={0} {...panelSurfaceProps}>
      <PanelHeader title="Channels"
        action={
          <Switch.Root checked={config.autoHopEnabled} colorPalette="green" size="sm"
            disabled={!canOperate || (config.autoHopEnabled && (!state.connected || !currentInList))}
            onCheckedChange={(details) => {
              if (details.checked === config.autoHopEnabled) return;
              if (details.checked) void change("auto", null);
              else if (state.current !== null && currentInList) void change("manual", state.current);
            }}
            display="flex" alignItems="center" gap={2}>
            <Switch.HiddenInput />
            <Switch.Control><Switch.Thumb /></Switch.Control>
            <Switch.Label fontSize="11px" color="gray.200" whiteSpace="nowrap">自动选频</Switch.Label>
          </Switch.Root>
        } borderBottom compact />
      <VStack align="stretch" gap={2} p={3} minH={fillHeight ? 0 : "530px"}
        flex={fillHeight ? "1" : undefined} overflowY={fillHeight ? "auto" : undefined}
        css={fillHeight ? hiddenScrollbarStyle : undefined}>
        {items.length === 0 ? (
          <Text fontSize="sm" color="gray.400">No score telemetry</Text>
        ) : items.map((item) => {
          const active = state.current !== null ? item.channel === state.current : item.active;
          const selectable = canOperate && state.connected && !config.autoHopEnabled && item.channel !== state.current;
          return (
            <Button key={item.channel} disabled={!selectable} variant="plain" display="block" h="auto" w="100%"
              aria-label={`切换到频道 ${item.channel}`} aria-current={active ? "true" : undefined}
              onClick={() => { if (selectable) void change("manual", item.channel); }}
              cursor={selectable ? "pointer" : "default"} textAlign="left"
              borderWidth="1px" borderRadius="md"
              borderColor={active ? activeBorder : "rgba(92,255,138,0.12)"}
              bg={active ? activeBg : "rgba(0,0,0,0.16)"}
              px={3} py={2} boxShadow={active ? activeShadow : undefined}
              _hover={selectable ? { borderColor: "rgba(96,165,250,0.72)", bg: "rgba(96,165,250,0.11)" } : undefined}
              _disabled={{ opacity: 1 }}
              _focusVisible={{ outline: "2px solid", outlineColor: "blue.300" }}>
              <HStack justify="space-between" align="center">
                <HStack gap={2}>
                  <Badge colorPalette={active ? activePalette : "gray"}>#{item.rank}</Badge>
                  <Text fontSize="sm" color="gray.100" fontWeight="semibold">CH {item.channel}</Text>
                </HStack>
                <Badge colorPalette={isV3 ? (item.score === 65535 ? "gray" : item.score < 20 ? "green" : item.score < 50 ? "yellow" : "red") : scoreColor(item.score)}>
                  {item.score === 65535 ? "Unknown" : isV3 ? `${(item.score / 10).toFixed(1)}%` : item.score}
                </Badge>
              </HStack>
              <Box mt={2} h="5px" borderRadius="999px" bg="rgba(255,255,255,0.08)" overflow="hidden">
                <Box h="100%" w={`${item.score === 65535 ? 0 : Math.max(2, Math.min(100, item.score / 10))}%`}
                  bg={item.score <= 120 ? neonGreen : item.score <= 400 ? "rgba(255,214,92,0.86)" : "rgba(255,96,96,0.86)"} />
              </Box>
            </Button>
          );
        })}
        {config.autoHopEnabled && !state.connected ?
          <Text fontSize="xs" color="gray.400">收到当前频道的有效输入后，可关闭自动选频。</Text> : null}
        {result ? <Text fontSize="xs" color={`${resultTone}.200`} role="status">{result}</Text> : null}
      </VStack>
    </Box>
  );
}
