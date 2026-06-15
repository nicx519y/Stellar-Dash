import { Box } from "@chakra-ui/react";
import * as echarts from "echarts";
import * as React from "react";

import type { ChannelSwitchRow } from "./useMonitorStream";

export type RatePoint = { tMs: number; hz: number };
export type LossPoint = { tMs: number; value: number };

type ChartPoint = [number, number];
type EventPoint = [number, number, ChannelSwitchRow];

const CHANNEL_EVENT_LIMIT = 80;
const DEFAULT_WINDOW_MS = 30000;
const MIN_WINDOW_MS = 3000;
const FOLLOW_RIGHT_TOLERANCE_MS = 1200;

function formatTime(timestampMs: number) {
  return new Date(timestampMs).toLocaleTimeString();
}

function formatChannel(row: ChannelSwitchRow) {
  const from = typeof row.from === "number" ? row.from : "?";
  const to = typeof row.to === "number" ? row.to : typeof row.target === "number" ? row.target : "?";
  return `${from}->${to}`;
}

function clampPercent(value: number) {
  if (!Number.isFinite(value)) return 0;
  return Math.max(0, Math.min(100, value));
}

function normalizeRateData(points: RatePoint[]): ChartPoint[] {
  const latestByTime = new Map<number, number>();
  for (const point of points) {
    if (!Number.isFinite(point.tMs) || !Number.isFinite(point.hz)) continue;
    latestByTime.set(Math.trunc(point.tMs), Math.max(0, point.hz));
  }
  return [...latestByTime.entries()]
    .sort(([a], [b]) => a - b)
    .map(([tMs, hz]) => [tMs, Number(hz.toFixed(2))]);
}

function normalizeLossData(points: LossPoint[]): ChartPoint[] {
  const latestByTime = new Map<number, number>();
  for (const point of points) {
    if (!Number.isFinite(point.tMs) || !Number.isFinite(point.value)) continue;
    latestByTime.set(Math.trunc(point.tMs), clampPercent(point.value));
  }
  return [...latestByTime.entries()]
    .sort(([a], [b]) => a - b)
    .map(([tMs, value]) => [tMs, Number(value.toFixed(3))]);
}

function isChannelEventVisible(row: ChannelSwitchRow) {
  return (
    row.type === "channel_change" ||
    row.type === "hop_start" ||
    row.type === "hop_finish" ||
    row.type === "target_change" ||
    row.type === "link_lost" ||
    row.type === "link_recovered"
  );
}

function normalizeEventData(rows: ChannelSwitchRow[]): EventPoint[] {
  return rows
    .filter(isChannelEventVisible)
    .slice(-CHANNEL_EVENT_LIMIT)
    .filter((row) => Number.isFinite(row.timestampMs))
    .map((row) => [Math.trunc(row.timestampMs), 1, row]);
}

function tooltipFormatter(params: unknown) {
  const items = Array.isArray(params) ? params : [params];
  const first = items[0] as { axisValue?: number | string; value?: unknown } | undefined;
  const axisValue = Number(first?.axisValue ?? (Array.isArray(first?.value) ? first.value[0] : Date.now()));
  const lines = [`<div style="margin-bottom:4px;color:#cbd5e0">${formatTime(axisValue)}</div>`];

  for (const item of items as Array<{ seriesName?: string; marker?: string; value?: unknown }>) {
    if (!Array.isArray(item.value)) continue;
    if (item.seriesName === "Report Rate") {
      lines.push(`${item.marker ?? ""}Report rate: ${Number(item.value[1] ?? 0).toFixed(1)} Hz`);
      continue;
    }
    if (item.seriesName === "Packet Loss") {
      lines.push(`${item.marker ?? ""}Packet loss: ${Number(item.value[1] ?? 0).toFixed(2)} %`);
      continue;
    }
    if (item.seriesName === "Channel Events") {
      const row = item.value[2] as ChannelSwitchRow | undefined;
      if (!row) continue;
      const loss = typeof row.lossPercent === "number" ? `, loss ${row.lossPercent.toFixed(2)}%` : "";
      lines.push(`${item.marker ?? ""}Channel ${formatChannel(row)}: ${row.reason}${loss}`);
    }
  }

  return lines.join("<br/>");
}

export function TelemetryTrendChart({
  rateSeries,
  lossSeries,
  channelSwitches,
  height = 360,
}: {
  rateSeries: RatePoint[];
  lossSeries: LossPoint[];
  channelSwitches: ChannelSwitchRow[];
  height?: number | string;
}) {
  const rootRef = React.useRef<HTMLDivElement | null>(null);
  const chartRef = React.useRef<echarts.ECharts | null>(null);
  const chartRangeRef = React.useRef({ maxTime: 0 });
  const suppressZoomEventRef = React.useRef(false);
  const zoomRef = React.useRef({
    followRight: true,
    start: 0,
    end: 0,
    span: DEFAULT_WINDOW_MS,
  });

  const chartData = React.useMemo(() => {
    const rateData = normalizeRateData(rateSeries);
    const lossData = normalizeLossData(lossSeries);
    const eventData = normalizeEventData(channelSwitches);
    const times = [
      ...rateData.map(([tMs]) => tMs),
      ...lossData.map(([tMs]) => tMs),
      ...eventData.map(([tMs]) => tMs),
    ];
    const maxLoss = Math.max(5, ...lossData.map(([, value]) => value));
    const minTime = times.length > 0 ? Math.min(...times) : Date.now() - 1000;
    const maxTime = times.length > 0 ? Math.max(...times) : Date.now();
    return {
      rateData,
      lossData,
      eventData,
      minTime,
      maxTime: maxTime > minTime ? maxTime : minTime + 1000,
      maxLoss: Math.min(100, Math.ceil(maxLoss * 1.2)),
    };
  }, [channelSwitches, lossSeries, rateSeries]);
  chartRangeRef.current = { maxTime: chartData.maxTime };

  React.useEffect(() => {
    if (!rootRef.current) return;
    const chart = echarts.init(rootRef.current, undefined, { renderer: "canvas" });
    chartRef.current = chart;
    chart.on("dataZoom", () => {
      if (suppressZoomEventRef.current) return;
      const dataZoom = chart.getOption().dataZoom;
      const zoom = Array.isArray(dataZoom) ? dataZoom[0] : undefined;
      const start = Number((zoom as { startValue?: unknown } | undefined)?.startValue);
      const end = Number((zoom as { endValue?: unknown } | undefined)?.endValue);
      if (!Number.isFinite(start) || !Number.isFinite(end) || end <= start) return;
      zoomRef.current = {
        followRight: chartRangeRef.current.maxTime - end <= FOLLOW_RIGHT_TOLERANCE_MS,
        start,
        end,
        span: Math.max(MIN_WINDOW_MS, end - start),
      };
    });

    const resize = () => chart.resize();
    window.addEventListener("resize", resize);
    return () => {
      window.removeEventListener("resize", resize);
      chart.dispose();
      chartRef.current = null;
    };
  }, []);

  React.useEffect(() => {
    const chart = chartRef.current;
    if (!chart) return;

    const zoom = zoomRef.current;
    let zoomEnd = chartData.maxTime;
    let zoomStart = Math.max(chartData.minTime, zoomEnd - zoom.span);
    if (!zoom.followRight && zoom.end > zoom.start) {
      zoomStart = Math.max(chartData.minTime, zoom.start);
      zoomEnd = Math.min(chartData.maxTime, zoom.end);
      if (zoomEnd - zoomStart < MIN_WINDOW_MS) {
        zoomEnd = Math.min(chartData.maxTime, zoomStart + MIN_WINDOW_MS);
      }
    }
    zoomRef.current = {
      followRight: zoom.followRight,
      start: zoomStart,
      end: zoomEnd,
      span: Math.max(MIN_WINDOW_MS, zoomEnd - zoomStart),
    };
    suppressZoomEventRef.current = true;
    chart.setOption(
      {
        backgroundColor: "transparent",
        animation: false,
        color: ["#62f7ff", "#ff6b6b", "#f6ad55"],
        tooltip: {
          trigger: "axis",
          confine: true,
          backgroundColor: "rgba(5,8,13,0.96)",
          borderColor: "rgba(92,255,138,0.22)",
          textStyle: { color: "#edf2f7", fontSize: 12 },
          axisPointer: {
            type: "cross",
            lineStyle: { color: "rgba(92,255,138,0.34)" },
            crossStyle: { color: "rgba(92,255,138,0.22)" },
          },
          formatter: tooltipFormatter,
        },
        legend: {
          top: 0,
          right: 0,
          textStyle: { color: "#b7c4bd" },
          data: ["Report Rate", "Packet Loss", "Channel Events"],
        },
        grid: {
          left: 54,
          right: 58,
          top: 38,
          bottom: 64,
        },
        dataZoom: [
          {
            type: "inside",
            xAxisIndex: 0,
            filterMode: "filter",
            startValue: zoomStart,
            endValue: zoomEnd,
            minValueSpan: MIN_WINDOW_MS,
          },
          {
            type: "slider",
            xAxisIndex: 0,
            filterMode: "filter",
            height: 18,
            bottom: 14,
            startValue: zoomStart,
            endValue: zoomEnd,
            minValueSpan: MIN_WINDOW_MS,
            borderColor: "rgba(92,255,138,0.18)",
            fillerColor: "rgba(92,255,138,0.16)",
            handleStyle: { color: "#5cff8a" },
            moveHandleStyle: { color: "#5cff8a" },
            textStyle: { color: "#a0aec0" },
            dataBackground: {
              lineStyle: { color: "rgba(92,255,138,0.35)" },
              areaStyle: { color: "rgba(92,255,138,0.08)" },
            },
            selectedDataBackground: {
              lineStyle: { color: "rgba(92,255,138,0.65)" },
              areaStyle: { color: "rgba(92,255,138,0.16)" },
            },
          },
        ],
        xAxis: {
          type: "time",
          min: chartData.minTime,
          max: chartData.maxTime,
          axisLabel: {
            color: "#a0aec0",
            formatter: (value: number) => formatTime(value),
          },
          axisLine: { lineStyle: { color: "rgba(92,255,138,0.18)" } },
          splitLine: { show: true, lineStyle: { color: "rgba(92,255,138,0.07)" } },
        },
        yAxis: [
          {
            type: "value",
            name: "Hz",
            min: 0,
            scale: false,
            axisLabel: { color: "#a0aec0" },
            nameTextStyle: { color: "#a0aec0" },
            axisLine: { lineStyle: { color: "rgba(98,247,255,0.5)" } },
            splitLine: { show: true, lineStyle: { color: "rgba(92,255,138,0.08)" } },
          },
          {
            type: "value",
            name: "%",
            min: 0,
            max: chartData.maxLoss,
            axisLabel: { color: "#a0aec0", formatter: "{value}%" },
            nameTextStyle: { color: "#a0aec0" },
            axisLine: { lineStyle: { color: "rgba(255,107,107,0.5)" } },
            splitLine: { show: false },
          },
          {
            type: "value",
            min: 0,
            max: 1,
            show: false,
          },
        ],
        series: [
          {
            name: "Report Rate",
            type: "line",
            yAxisIndex: 0,
            data: chartData.rateData,
            showSymbol: false,
            symbol: "none",
            smooth: false,
            clip: true,
            lineStyle: { width: 2 },
          },
          {
            name: "Packet Loss",
            type: "line",
            yAxisIndex: 1,
            data: chartData.lossData,
            showSymbol: false,
            symbol: "none",
            smooth: false,
            clip: true,
            lineStyle: { width: 2 },
          },
          {
            name: "Channel Events",
            type: "scatter",
            yAxisIndex: 2,
            data: chartData.eventData,
            symbol: "pin",
            symbolSize: 24,
            itemStyle: {
              color: "#f6ad55",
              borderColor: "rgba(5,8,13,0.92)",
              borderWidth: 1,
            },
            label: {
              show: true,
              color: "#0b0f16",
              fontSize: 10,
              formatter: (param: { value?: unknown }) => {
                if (!Array.isArray(param.value)) return "";
                return formatChannel(param.value[2] as ChannelSwitchRow);
              },
            },
          },
        ],
      },
      true,
    );
    chart.resize();
    requestAnimationFrame(() => {
      suppressZoomEventRef.current = false;
    });
  }, [chartData]);

  return (
    <Box
      ref={rootRef}
      w="100%"
      h={typeof height === "number" ? `${height}px` : height}
      flex="1"
      minH={0}
      borderRadius="md"
      bg="transparent"
    />
  );
}
