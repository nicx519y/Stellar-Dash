import { Box } from "@chakra-ui/react";
import * as echarts from "echarts";
import * as React from "react";

import type { ChannelSwitchRow } from "./useMonitorStream";
import { neonGreen } from "./panelStyles";

export type RatePoint = { tMs: number; hz: number };
export type LossPoint = { tMs: number; value: number };
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

function sortedRateData(points: RatePoint[]) {
  return [...points]
    .sort((a, b) => a.tMs - b.tMs)
    .map((point) => [point.tMs, Number(point.hz.toFixed(2))]);
}

function sortedLossData(points: LossPoint[]) {
  return [...points]
    .sort((a, b) => a.tMs - b.tMs)
    .map((point) => [point.tMs, Number(point.value.toFixed(3))]);
}

function tooltipFormatter(params: unknown, channelSwitches: ChannelSwitchRow[]) {
  const items = Array.isArray(params) ? params : [params];
  const first = items[0] as { axisValue?: number | string; value?: unknown } | undefined;
  const axisValue = Number(first?.axisValue ?? (Array.isArray(first?.value) ? first?.value[0] : Date.now()));
  const lines = [`<div style="margin-bottom:4px;color:#cbd5e0">${formatTime(axisValue)}</div>`];

  for (const item of items as Array<{ seriesName?: string; marker?: string; value?: unknown }>) {
    if (!Array.isArray(item.value)) continue;
    const value = Number(item.value[1] ?? 0);
    if (item.seriesName === "Report Rate") {
      lines.push(`${item.marker ?? ""}Report rate: ${value.toFixed(1)} Hz`);
    } else if (item.seriesName === "Packet Loss") {
      lines.push(`${item.marker ?? ""}Packet loss: ${value.toFixed(2)} %`);
    } else if (item.seriesName === "Channel Events") {
      const row = item.value[2] as ChannelSwitchRow | undefined;
      if (row) {
        lines.push(`${item.marker ?? ""}Channel: ${formatChannel(row)} ${row.reason}`);
      }
    }
  }

  const nearby = channelSwitches.filter((row) => Math.abs(row.timestampMs - axisValue) <= 750).slice(-3);
  for (const row of nearby) {
    const loss = typeof row.lossPercent === "number" ? `, loss ${row.lossPercent.toFixed(2)}%` : "";
    lines.push(`<span style="color:#f6ad55">Channel switch ${formatChannel(row)}: ${row.reason}${loss}</span>`);
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
  const suppressZoomEventRef = React.useRef(false);
  const chartTimesRef = React.useRef({ minTime: 0, maxTime: 0 });
  const windowRef = React.useRef({
    initialized: false,
    followRight: true,
    start: 0,
    end: 0,
    span: DEFAULT_WINDOW_MS,
  });

  const chartTimes = React.useMemo(() => {
    const switchRows = channelSwitches.slice(-60);
    const allTimes = [
      ...rateSeries.map((point) => point.tMs),
      ...lossSeries.map((point) => point.tMs),
      ...switchRows.map((row) => row.timestampMs),
    ];
    const maxTime = allTimes.length > 0 ? Math.max(...allTimes) : Date.now();
    const minTime = allTimes.length > 0 ? Math.min(...allTimes) : maxTime - 1000;
    return { minTime, maxTime };
  }, [channelSwitches, lossSeries, rateSeries]);
  chartTimesRef.current = chartTimes;

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
      const span = Math.max(MIN_WINDOW_MS, end - start);
      const { maxTime } = chartTimesRef.current;
      windowRef.current = {
        initialized: true,
        followRight: maxTime - end <= FOLLOW_RIGHT_TOLERANCE_MS,
        start,
        end,
        span,
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

    const switchRows = channelSwitches.slice(-60);
    const maxLoss = Math.max(5, ...lossSeries.map((point) => point.value));
    const zoomWindow = windowRef.current;
    let zoomEnd = chartTimes.maxTime;
    let zoomStart = Math.max(chartTimes.minTime, zoomEnd - DEFAULT_WINDOW_MS);
    if (zoomWindow.initialized) {
      if (zoomWindow.followRight) {
        zoomEnd = chartTimes.maxTime;
        zoomStart = Math.max(chartTimes.minTime, zoomEnd - zoomWindow.span);
      } else {
        zoomStart = Math.max(chartTimes.minTime, zoomWindow.start);
        zoomEnd = Math.min(chartTimes.maxTime, zoomWindow.end);
        if (zoomEnd - zoomStart < MIN_WINDOW_MS) {
          zoomEnd = Math.min(chartTimes.maxTime, zoomStart + MIN_WINDOW_MS);
        }
      }
    }
    windowRef.current = {
      initialized: true,
      followRight: zoomWindow.followRight,
      start: zoomStart,
      end: zoomEnd,
      span: Math.max(MIN_WINDOW_MS, zoomEnd - zoomStart),
    };
    const markLines = switchRows.map((row) => ({
      name: formatChannel(row),
      xAxis: row.timestampMs,
      label: {
        formatter: formatChannel(row),
      },
    }));

    suppressZoomEventRef.current = true;
    chart.setOption(
      {
        backgroundColor: "transparent",
        color: [neonGreen, "#ff6b6b", "#62f7ff"],
        animation: false,
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
          formatter: (params: unknown) => tooltipFormatter(params, switchRows),
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
            filterMode: "none",
            startValue: zoomStart,
            endValue: zoomEnd,
            minValueSpan: MIN_WINDOW_MS,
          },
          {
            type: "slider",
            xAxisIndex: 0,
            filterMode: "none",
            height: 18,
            bottom: 14,
            startValue: zoomStart,
            endValue: zoomEnd,
            minValueSpan: MIN_WINDOW_MS,
            borderColor: "rgba(92,255,138,0.18)",
            fillerColor: "rgba(92,255,138,0.16)",
            handleStyle: { color: neonGreen },
            moveHandleStyle: { color: neonGreen },
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
          min: chartTimes.minTime,
          max: chartTimes.maxTime,
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
            axisLabel: { color: "#a0aec0" },
            nameTextStyle: { color: "#a0aec0" },
            axisLine: { lineStyle: { color: "rgba(92,255,138,0.48)" } },
            splitLine: { show: true, lineStyle: { color: "rgba(92,255,138,0.08)" } },
          },
          {
            type: "value",
            name: "%",
            min: 0,
            max: Math.ceil(maxLoss * 1.2),
            axisLabel: { color: "#a0aec0", formatter: "{value}%" },
            nameTextStyle: { color: "#a0aec0" },
            axisLine: { lineStyle: { color: "rgba(255,107,107,0.5)" } },
            splitLine: { show: false },
          },
        ],
        series: [
          {
            name: "Report Rate",
            type: "line",
            yAxisIndex: 0,
            showSymbol: false,
            smooth: false,
            connectNulls: true,
            lineStyle: { width: 2 },
            emphasis: { focus: "series" },
            data: sortedRateData(rateSeries),
            markLine: {
              silent: true,
              symbol: "none",
              label: {
                color: "#b9ffe4",
                fontSize: 11,
                position: "insideEndTop",
              },
              lineStyle: {
                color: "rgba(98,247,255,0.58)",
                type: "dashed",
                width: 1,
              },
              data: markLines,
            },
          },
          {
            name: "Packet Loss",
            type: "line",
            yAxisIndex: 1,
            showSymbol: false,
            smooth: false,
            connectNulls: true,
            lineStyle: { width: 2 },
            emphasis: { focus: "series" },
            data: sortedLossData(lossSeries),
          },
          {
            name: "Channel Events",
            type: "scatter",
            yAxisIndex: 1,
            symbol: "pin",
            symbolSize: 28,
            label: {
              show: true,
              color: "#0b0f16",
              fontSize: 10,
              formatter: (param: { value?: unknown }) => {
                if (!Array.isArray(param.value)) return "";
                return formatChannel(param.value[2] as ChannelSwitchRow);
              },
            },
            itemStyle: {
              color: "#62f7ff",
              borderColor: "rgba(5,8,13,0.92)",
              borderWidth: 1,
            },
            tooltip: {
              trigger: "item",
            },
            data: switchRows.map((row) => [row.timestampMs, row.lossPercent ?? 0, row]),
          },
        ],
      },
      true,
    );
    chart.resize();
    requestAnimationFrame(() => {
      suppressZoomEventRef.current = false;
    });
  }, [channelSwitches, chartTimes.maxTime, chartTimes.minTime, lossSeries, rateSeries]);

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
