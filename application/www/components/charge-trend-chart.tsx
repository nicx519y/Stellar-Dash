'use client';
import { Box, HStack, Text } from '@chakra-ui/react';
import { Chart as ChartJS, LinearScale, LineElement, PointElement, Tooltip, type ChartOptions, type Plugin } from 'chart.js';
import { Line } from 'react-chartjs-2';
import { useColorModeValue } from './ui/color-mode';
import { chargeTrend, type ChargeSession } from '@/lib/charge-test-logs';
import { WEB_FONT_FAMILY } from '@/lib/ui-typography';
import { chartWebFont } from '@/lib/chart-web-font';

ChartJS.register(LinearScale, LineElement, PointElement, Tooltip, chartWebFont);
export function ChargeTrendChart({ session, language }: { session: ChargeSession; language: 'zh' | 'en' }) {
  const zh = language === 'zh';
  const axis = useColorModeValue('#71717a', '#92929d');
  const grid = useColorModeValue('rgba(0,0,0,.07)', 'rgba(255,255,255,.07)');
  const voltage = useColorModeValue('#0d8a7d', '#55c9b7');
  const soc = useColorModeValue('#7960c9', '#b09bee');
  const current = useColorModeValue('#bc720d', '#edb55e');
  const points = chargeTrend(session);
  const origin = session.start?.uptimeMs ?? session.trend[0]?.data[2] ?? 0;
  const markers = session.items.filter(r => r.data.length === 14 && [121, 125, 127].includes(r.event)).map(r => ({
    x: ((r.uptimeMs - origin) >>> 0) / 3600000,
    label: r.event === 121 ? (zh ? '充电段开始' : 'Segment start') : r.event === 127 ?
      (r.data[3] === 1 ? zh ? '采样恢复' : 'Samples restored' : zh ? '采样缺口' : 'Sample gap') :
      r.data[3] === 1 ? zh ? '确认充满' : 'Full confirmed' : r.data[3] === 6 ? zh ? '拔电' : 'Unplugged' : zh ? '充电段结束' : 'Segment end',
  }));
  const maxHours = Math.max(1 / 60, ...points.map(p => p.x), ...markers.map(m => m.x));
  const font = { family: WEB_FONT_FAMILY, size: 10 };
  const annotations: Plugin<'line'> = { id: 'chargeEvents', afterDatasetsDraw(chart) {
    const { ctx, chartArea, scales } = chart;
    ctx.save(); ctx.strokeStyle = axis; ctx.globalAlpha = .35; ctx.setLineDash([2, 4]);
    for (const m of markers) {
      const x = scales.x.getPixelForValue(m.x);
      ctx.beginPath(); ctx.moveTo(x, chartArea.top); ctx.lineTo(x, chartArea.bottom); ctx.stroke();
    }
    ctx.restore();
  } };
  return <Box>
    {[false, true].map(supply => {
      const labels = supply ? [zh ? '输入电压' : 'Input voltage', zh ? '充电电流' : 'Charge current'] : [zh ? '电池电压' : 'Battery voltage', 'SOC'];
      const colors = [voltage, supply ? current : soc];
      const units = ['V', supply ? 'mA' : '%'];
      const options: ChartOptions<'line'> = {
        responsive: true, maintainAspectRatio: false, animation: false, interaction: { mode: 'index', intersect: false },
        plugins: { legend: { display: false }, tooltip: { titleFont: font, bodyFont: font,
          callbacks: {
            title: items => `${zh ? '已运行' : 'Elapsed'} ${Number(items[0]?.parsed.x ?? 0).toFixed(2)} h`,
            label: item => `${item.dataset.label}: ${Number(item.parsed.y).toFixed(item.datasetIndex === 0 ? 2 : 1)} ${units[item.datasetIndex]}`,
            afterBody: items => {
              const p = points[items[0]?.dataIndex ?? -1];
              if (!p) return [];
              return [supply ? `${(p.inputMin / 1000).toFixed(2)}–${(p.inputMax / 1000).toFixed(2)} V · ${p.currentMin}–${p.currentMax} mA` :
                `${(p.cellMin / 1000).toFixed(2)}–${(p.cellMax / 1000).toFixed(2)} V · SOC ${p.firstSoc.toFixed(1)}→${p.lastSoc.toFixed(1)}%`,
                `${zh ? '有效 / 缺失采样' : 'Valid / missing samples'}: ${p.validSamples} / ${p.missingSamples}`];
            },
          } } },
        scales: {
          x: { type: 'linear', min: 0, max: maxHours, grid: { display: false }, border: { display: false }, ticks: { color: axis, font, maxTicksLimit: 5, maxRotation: 0, callback: v => `${Number(v).toFixed(2)} h` } },
          left: { type: 'linear', position: 'left', border: { display: false }, grid: { color: grid }, ticks: { color: axis, font, maxTicksLimit: 5, callback: v => `${Number(v).toFixed(2)} V` } },
          right: { type: 'linear', position: 'right', min: 0, ...(supply ? {} : { max: 100 }), border: { display: false }, grid: { drawOnChartArea: false }, ticks: { color: axis, font, maxTicksLimit: 5, callback: v => `${Number(v).toFixed(0)} ${units[1]}` } },
        },
      };
      return <Box key={String(supply)} mt={4} minW={0}>
        <HStack justify="space-between" flexWrap="wrap" mb={2} fontSize="11px" color="fg.muted">
          <Text>{supply ? zh ? '输入电压与充电电流' : 'Input voltage and charging current' : zh ? '电量与电池电压' : 'SOC and battery voltage'}</Text>
          <HStack gap={3}>{labels.map((label, i) => <HStack key={label} gap={1}><Box w="12px" borderTopWidth="2px" borderStyle={i ? 'dashed' : 'solid'} borderColor={colors[i]} /><Text>{label}</Text></HStack>)}</HStack>
        </HStack>
        <Box height={{ base: '180px', md: '210px' }}>
          <Line role="img" aria-label={labels.join(' / ')} plugins={[annotations]} options={options} data={{ datasets: labels.map((label, i) => ({
            label, yAxisID: i ? 'right' : 'left', borderColor: colors[i], borderDash: i ? [4, 4] : [],
            borderWidth: 1.8, pointRadius: i ? 0 : 1.5, pointHoverRadius: 4, tension: 0, spanGaps: false,
            data: points.map(p => ({ x: p.x, y: !p.valid ? null : supply ? (i ? p.currentMean : p.inputMean / 1000) : (i ? p.lastSoc : p.cellMean / 1000) })),
          })) }} />
        </Box>
      </Box>;
    })}
    {!points.some(p => p.valid) && <Text mt={2} color="orange.fg" fontSize="11px">{zh ? '暂无有效五分钟摘要；不足五分钟的尾段会在结束时保存。' : 'No valid trend summaries yet. The final partial bucket is saved when the test ends.'}</Text>}
    <Text mt={2} color="fg.muted" fontSize="10px">{zh ? '曲线：五分钟均值 / 末尾 SOC；竖虚线：状态事件；缺失采样处断开。' : 'Five-minute means / final SOC. Vertical lines mark events; sample gaps break the curves.'}</Text>
    <HStack gapX={3} gapY={1} flexWrap="wrap" mt={2} fontSize="10px" color="fg.muted">
      {markers.map((m, i) => <Text key={i}>{m.x.toFixed(2)} h · {m.label}</Text>)}
    </HStack>
  </Box>;
}
