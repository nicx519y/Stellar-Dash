'use client';

import { Box, HStack, Text } from '@chakra-ui/react';
import { Chart as ChartJS, Filler, LinearScale, LineElement, PointElement, Tooltip, type ChartOptions } from 'chart.js';
import { Line } from 'react-chartjs-2';
import { useColorModeValue } from '@/components/ui/color-mode';
import { batteryTrendChart } from '@/lib/battery-trend-chart';
import type { FaultRecord } from '@/lib/device-fault-logs';
import { WEB_FONT_FAMILY } from '@/lib/ui-typography';
import { chartWebFont } from '@/lib/chart-web-font';

ChartJS.register(LinearScale, LineElement, PointElement, Tooltip, Filler, chartWebFont);

export function BatteryTrendChart({ trend, start, language }: { trend: FaultRecord[]; start?: FaultRecord; language: 'zh' | 'en' }) {
  const zh = language === 'zh';
  const axis = useColorModeValue('#71717a', '#92929d');
  const grid = useColorModeValue('rgba(0,0,0,0.07)', 'rgba(255,255,255,0.07)');
  const voltageColor = useColorModeValue('#0d8a7d', '#55c9b7');
  const socColor = useColorModeValue('#7960c9', '#b09bee');
  const data = batteryTrendChart(trend, start);
  const validVoltage = data.voltage.flatMap(point => point.y === null ? [] : [point.y]);
  const minVoltage = validVoltage.length ? Math.min(...validVoltage) : 3;
  const maxVoltage = validVoltage.length ? Math.max(...validVoltage) : 4.3;
  const voltageStep = maxVoltage - minVoltage > 0.5 ? 0.2 : 0.1;
  const voltageLabel = zh ? '最低电压' : 'Minimum voltage';
  const font = { family: WEB_FONT_FAMILY, size: 10 };
  const options: ChartOptions<'line'> = {
    responsive: true, maintainAspectRatio: false, animation: false,
    interaction: { mode: 'index', intersect: false },
    plugins: {
      legend: { display: false },
      tooltip: { padding: 10, displayColors: true, titleFont: font, bodyFont: { ...font, size: 11 },
        callbacks: {
          title: items => `${zh ? '已运行' : 'Elapsed'} ${Number(items[0]?.parsed.x ?? 0).toFixed(2)} h`,
          label: item => ` ${item.dataset.label}: ${Number(item.parsed.y).toFixed(item.datasetIndex === 0 ? 3 : 1)} ${item.datasetIndex === 0 ? 'V' : '%'}`,
        },
      },
    },
    scales: {
      x: { type: 'linear', min: 0, max: data.maxHours, border: { display: false }, grid: { display: false },
        ticks: { color: axis, font, maxTicksLimit: 5, maxRotation: 0, callback: value => `${Number(value).toFixed(2)} h` } },
      voltage: { type: 'linear', position: 'left',
        min: Math.floor((minVoltage - 0.04) / voltageStep) * voltageStep,
        max: Math.ceil((maxVoltage + 0.04) / voltageStep) * voltageStep,
        border: { display: false }, grid: { color: grid, drawTicks: false },
        ticks: { color: axis, font, padding: 8, stepSize: voltageStep, maxTicksLimit: 6, callback: value => `${Number(value).toFixed(1)} V` } },
      soc: { type: 'linear', position: 'right', min: 0, max: 100, border: { display: false }, grid: { drawOnChartArea: false, drawTicks: false },
        ticks: { color: axis, font, padding: 8, stepSize: 25, callback: value => `${value}%` } },
    },
  };
  const lastVoltage = data.voltage.filter(point => point.y !== null).at(-1)?.y;
  const lastSoc = data.soc.filter(point => point.y !== null).at(-1)?.y;
  return <Box minW="0">
    <HStack justify="space-between" gap={2} flexWrap="wrap" fontSize="11px" mb={3}>
      <Text color="fg.muted">{zh ? '会话采样趋势（含插电及恢复）' : 'Session trend (including external power and recovery)'}</Text>
      <HStack gap={4} flexWrap="wrap">
        <HStack gap={1.5}><Box w="12px" h="2px" bg={voltageColor} /><Text color="fg.muted">{voltageLabel}</Text><Text fontFamily="mono" color={voltageColor}>{lastVoltage?.toFixed(2) ?? '—'} V</Text></HStack>
        <HStack gap={1.5}><Box w="12px" borderTopWidth="2px" borderStyle="dashed" borderColor={socColor} /><Text color="fg.muted">SOC</Text><Text fontFamily="mono" color={socColor}>{lastSoc?.toFixed(1) ?? '—'}%</Text></HStack>
      </HStack>
    </HStack>
    {validVoltage.length ? <Box h={{ base: '170px', md: '200px' }} minW="0">
      <Line role="img" aria-label={zh ? '最低电压与剩余电量趋势，横轴为运行小时数' : 'Minimum voltage and remaining charge, with elapsed hours on the horizontal axis'}
        options={options} data={{ datasets: [
          { label: voltageLabel, data: data.voltage, yAxisID: 'voltage', borderColor: voltageColor, backgroundColor: `${voltageColor}0d`, borderWidth: 1.8, pointRadius: 1.5, pointHoverRadius: 4, fill: true, spanGaps: false, tension: 0, order: 2 },
          { label: 'SOC', data: data.soc, yAxisID: 'soc', borderColor: socColor, borderDash: [4, 4], borderWidth: 1.8, pointRadius: 0, pointHoverRadius: 4, spanGaps: false, tension: 0, order: 1 },
        ] }} />
    </Box> : <Box py={8} textAlign="center" borderWidth="1px" borderStyle="dashed" borderRadius="md" color="fg.muted" fontSize="12px">{zh ? '暂无连续有效采样' : 'No valid samples to plot'}</Box>}
  </Box>;
}
