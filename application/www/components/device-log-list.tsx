import { useCallback, useEffect, useLayoutEffect, useRef, useState, type ReactNode, type RefObject } from 'react';
import { logVirtualWindow } from '@/lib/log-virtual-window';
import { Badge, Box, Button, Collapsible, Grid, HStack, Text } from '@chakra-ui/react';
import { LuChevronRight } from 'react-icons/lu';
import { batteryEventName, decodeBatteryRecord } from '@/lib/battery-test-logs';
import type { FaultLogPage, FaultRecord } from '@/lib/device-fault-logs';
import { WEB_FONT_FAMILY } from '@/lib/ui-typography';
import { chargeSummary } from '@/lib/charge-test-logs';
import { gameLinkText } from '@/lib/game-link-logs';

const columns = { base: '80px 54px minmax(0, 1fr) 16px', md: '54px 112px 60px 72px minmax(0, 1fr) 44px 16px' };
const firmware = (value: number) => `${value >>> 16}.${(value >>> 8) & 255}.${value & 255}`;

function batteryDescription(record: FaultRecord, zh: boolean) {
  if (record.data[0] === 2 && record.data.length === 14) {
    if (record.event === 122) {
      const s = chargeSummary(record);
      return s.validSamples ? `${(s.inputMean / 1000).toFixed(2)} V / ${(s.cellMean / 1000).toFixed(2)} V · ${s.currentMean} mA · SOC ${s.firstSoc.toFixed(1)} → ${s.lastSoc.toFixed(1)}%${s.missingSamples ? ` · ${zh ? '缺失' : 'Missing'} ${s.missingSamples}` : ''}` : zh ? '无有效充电采样' : 'No valid charge samples';
    }
    if (record.event === 124) return `${record.data[3] / 10}% · ${(record.data[4] / 1000).toFixed(2)} V`;
    if (record.event === 123) {
      const d = record.data;
      const states = zh ? ['未充电', '充电中', '充满状态', '故障', '未知'] : ['Not charging', 'Charging', 'Full state', 'Fault', 'Unknown'];
      const blocks = zh ? ['允许充电', '未插电', '采样不可用', '配置未就绪', '输入电压异常', '充电故障', '故障恢复中', '电源保护'] :
        ['Charging allowed', 'No input', 'Telemetry unavailable', 'Profile not ready', 'Input out of range', 'Charger fault', 'Fault recovery', 'Power protection'];
      return `${states[d[8] & 255] ?? states[4]} · ${blocks[d[9]] ?? states[4]} · ${(d[6] / 1000).toFixed(2)} V · ${d[7]} mA`;
    }
  }
  if (record.event !== 102 || record.data.length !== 14) return '';
  const d = record.data;
  const sample = d[8] ? `${(d[4] / 1000).toFixed(2)}–${(d[5] / 1000).toFixed(2)} V · SOC ${(d[6] / 10).toFixed(1)} → ${(d[7] / 10).toFixed(1)}%`
    : zh ? '无有效电量采样' : 'No valid battery samples';
  return `${sample}${d[9] ? ` · ${zh ? '缺失' : 'Missing'} ${d[9]}` : ''}`;
}

function MeasuredRow({ children, onHeight }: { children: ReactNode; onHeight: (height: number) => void }) {
  const ref = useRef<HTMLDivElement>(null);
  useLayoutEffect(() => {
    const element = ref.current;
    if (!element) return;
    const observer = new ResizeObserver(() => onHeight(element.getBoundingClientRect().height));
    observer.observe(element); onHeight(element.getBoundingClientRect().height);
    return () => observer.disconnect();
  }, [onHeight]);
  return <Box ref={ref}>{children}</Box>;
}
export function DeviceLogList({ page, language, scrollRef }: { page: FaultLogPage; language: 'zh' | 'en'; scrollRef: RefObject<HTMLDivElement> }) {
  const bodyRef = useRef<HTMLDivElement>(null);
  const [viewport, setViewport] = useState({ top: 0, height: 600 });
  const [heights, setHeights] = useState<Record<string, number>>({});
  const [expanded, setExpanded] = useState<Record<string, boolean>>({});
  const keyOf = (record: FaultRecord, index: number) => `${page.source}-${record.boot}-${record.sequence}-${index}`;
  const measure = useCallback((key: string, height: number) => {
    if (height > 0) setHeights(current => current[key] === height ? current : { ...current, [key]: height });
  }, []);
  useEffect(() => {
    const scroller = scrollRef.current, body = bodyRef.current;
    if (!scroller || !body) return;
    const update = () => {
      const top = scroller.getBoundingClientRect().top - body.getBoundingClientRect().top;
      const height = scroller.clientHeight;
      setViewport(current => current.top === top && current.height === height ? current : { top, height });
    };
    const observer = new ResizeObserver(update);
    observer.observe(scroller); observer.observe(body);
    scroller.addEventListener('scroll', update, { passive: true }); update();
    return () => { observer.disconnect(); scroller.removeEventListener('scroll', update); };
  }, [scrollRef]);
  const window = logVirtualWindow(page.items.map((record, index) => heights[keyOf(record, index)] ?? 48), viewport.top, viewport.height);
  const zh = language === 'zh';
  const events = zh ? ['未知事件', '处理器异常', '致命错误', '角色启动失败', '板间通信异常', 'USB 异常', 'RF 异常', '链路恢复', '日志缓冲溢出']
    : ['Unknown event', 'CPU exception', 'Fatal error', 'Role startup failure', 'BoardLink fault', 'USB fault', 'RF fault', 'Link recovered', 'Log buffer overflow'];
  return <Box fontFamily={WEB_FONT_FAMILY} fontSize="12px" lineHeight="1.5" letterSpacing="normal">
    <Grid templateColumns={columns} gap={2} px={3} py={2} bg="bg.subtle" color="fg.muted" fontSize="10px" textTransform="uppercase" letterSpacing="0.06em" borderBottomWidth="1px">
      <Text display={{ base: 'none', md: 'block' }}>SEQ</Text>
      <Text>{zh ? '运行时间' : 'Uptime'}</Text>
      <Text display={{ base: 'none', md: 'block' }}>BOOT</Text>
      <Text>{zh ? '级别' : 'Level'}</Text>
      <Text>{zh ? '事件 / 数据' : 'Event / Data'}</Text>
      <Text display={{ base: 'none', md: 'block' }} textAlign="right">{zh ? '次数' : 'Count'}</Text>
      <Box />
    </Grid>
    <Box ref={bodyRef} position="relative" height={`${window.total}px`} data-testid="virtual-log-list" data-total-records={page.items.length}>
    <Box position="absolute" top={`${window.offsets[window.start]}px`} left={0} right={0}>
    {page.items.slice(window.start, window.end).map((record, localIndex) => {
      const index = window.start + localIndex;
      const key = keyOf(record, index);
      const level = record.severity === 4 ? 'FATAL' : record.severity === 3 ? 'ERROR' : record.severity === 1 ? 'INFO' : 'WARN';
      const palette = record.severity === 4 ? 'red' : record.severity === 3 ? 'orange' : record.severity === 1 ? 'blue' : 'yellow';
      const connection = page.source !== 'battery' ? gameLinkText(record, language) : null;
      const name = connection?.name ?? (page.source === 'battery' ? batteryEventName(record.event, language) : undefined) ?? events[record.event] ?? `${zh ? '事件' : 'Event'} ${record.event}`;
      const description = connection?.description ?? (page.source === 'battery' ? batteryDescription(record, zh) : '');
      return <MeasuredRow key={key} onHeight={height => measure(key, height)}><Collapsible.Root lazyMount unmountOnExit
        open={!!expanded[key]} onOpenChange={details => setExpanded(current => ({ ...current, [key]: details.open }))}
        borderBottomWidth="1px">
        <Collapsible.Trigger asChild>
          <Button variant="plain" borderRadius="0" w="full" h="auto" minH="36px" px={3} py={2} display="grid" gridTemplateColumns={columns} gap={2}
            fontFamily="inherit" fontSize="inherit" fontWeight="normal" lineHeight="inherit" textAlign="left" color="fg" bg={index % 2 ? 'bg.subtle' : 'transparent'}
            _hover={{ bg: 'bg.muted' }} _focusVisible={{ outline: '2px solid', outlineColor: 'green.500', outlineOffset: '-2px' }}
            css={{ '&[data-state=open] .log-chevron': { transform: 'rotate(90deg)' } }}
            aria-label={`${zh ? '展开记录详情' : 'Expand record details'}: ${name}, #${record.sequence}`}>
            <Text color="fg.muted" display={{ base: 'none', md: 'block' }} fontVariantNumeric="tabular-nums">{String(record.sequence).padStart(4, '0')}</Text>
            <Text color="fg.muted" fontSize={{ base: '10px', md: '12px' }} fontVariantNumeric="tabular-nums" title={`${record.uptimeMs} ms`}>+{(record.uptimeMs / 1000).toFixed(3)}<Box as="span" fontSize="10px" ml="1">s</Box></Text>
            <Text color="fg.muted" display={{ base: 'none', md: 'block' }}>#{record.boot}</Text>
            <Badge colorPalette={palette} variant="subtle" fontFamily="inherit" fontSize="10px" fontWeight="medium" px="1.5" py="0" justifySelf="start">{level}</Badge>
            <Box minW="0" whiteSpace="normal">
              <HStack gap={2} flexWrap="wrap"><Text fontSize={{ base: '11px', md: '12px' }} fontWeight="medium">{name}</Text>
                {record.pending && <Badge colorPalette="orange" variant="outline" fontSize="9px">{zh ? '待落盘' : 'Pending'}</Badge>}</HStack>
              {description && <Text fontSize="10px" color="fg.muted" mt="0.5">{description}</Text>}
            </Box>
            <Text color="fg.muted" display={{ base: 'none', md: 'block' }} textAlign="right">×{record.repeat}</Text>
            <Box as={LuChevronRight} className="log-chevron" color="fg.muted" boxSize="12px" />
          </Button>
        </Collapsible.Trigger>
        <Collapsible.Content>
          <Box bg="bg.subtle" borderTopWidth="1px" px={{ base: 3, md: 5 }} py={3} fontSize="11px">
            <HStack gapX={4} gapY={1} flexWrap="wrap" color="fg.muted" mb={2}>
              <Text>seq=<Box as="span" color="fg">{record.sequence}</Box></Text><Text>boot={record.boot}</Text>
              <Text>uptime={record.uptimeMs} ms</Text><Text>firmware={firmware(record.firmware)}</Text>
              <Text>event={record.event}</Text><Text>count={record.repeat}</Text>
            </HStack>
            {page.source === 'battery' && <Box as="pre" overflowX="auto" whiteSpace="pre" color="fg" mb={2}>{JSON.stringify(decodeBatteryRecord(record), null, 2)}</Box>}
            {connection && <Text mb={2}>{connection.details}<br />{page.source === 'stm32'
              ? zh ? '主控记录本地采样、角色及板间恢复结果，端点进度请结合 TX / RX 日志查看。' : 'Controller records describe local sampling, roles and board-link recovery. Check TX / RX logs for endpoint progress.'
              : zh ? '完成计数：TX 的 RF 模式表示有效 ACK，其余表示 USB 端点完成；不代表游戏已处理。' : 'Completions: valid ACKs for TX in RF mode; USB endpoint completions otherwise. This does not confirm game processing.'}</Text>}
            <HStack gapX={4} gapY={1} flexWrap="wrap" color="fg.muted">
              {record.data.map((value, i) => <Text key={i}>[{String(i).padStart(2, '0')}]=<Box as="span" color="fg">0x{value.toString(16).padStart(8, '0')}</Box></Text>)}
            </HStack>
          </Box>
        </Collapsible.Content>
      </Collapsible.Root></MeasuredRow>;
    })}
    </Box></Box>
  </Box>;
}
