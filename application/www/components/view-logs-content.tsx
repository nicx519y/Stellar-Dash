"use client";

import { useEffect, useRef, useState } from "react";
import { Box, Button, Heading, HStack, Text, VStack, Badge, Flex, Grid, Progress, createListCollection } from "@chakra-ui/react";
import { LuBatteryMedium, LuCpu, LuFileJson, LuFileText, LuRadio, LuRefreshCw, LuPlay, LuTrash2, LuScrollText } from 'react-icons/lu';
import { LogConfirmDialog } from '@/components/ui/log-confirm-dialog';
import { navigationEvents } from '@/lib/event-manager';
import { useLogOperation } from '@/lib/log-operation';
import { logReadPercent } from '@/lib/device-fault-logs';
import { PanelActionButton } from '@/components/ui/panel-action-button';
import { SETTINGS_SIDEBAR_WIDTH } from '@/lib/ui-layout';
import { SelectContent, SelectItem, SelectLabel, SelectRoot, SelectTrigger, SelectValueText } from "@/components/ui/select";
import { deviceDisplayText } from "@/lib/device-display-text";
import { BatteryTestLogSummary } from "./battery-test-log-summary";
import { DeviceLogList } from "./device-log-list";
import { useGamepadFields } from "@/contexts/gamepad-config-context";
import { useLanguage } from "@/contexts/language-context";
import { DeviceTransportState } from "@/lib/device-transport";
import {
  collectFaultLogs, logExport, logExportText,
  type FaultLogPage, type LogSource,
} from "@/lib/device-fault-logs";

const labels = {
  zh: {
    title: "设备日志", refresh: "刷新", loading: "读取中…", export: "导出",
    source: "来源", category: "类别", severity: "级别", all: "全部", controller: "主控",
    fault: "致命异常", link: "异常链路", recovery: "恢复摘要", warning: "警告", error: "错误", fatal: "致命",
    disconnected: "连接设备后可读取日志。", empty: "暂无符合条件的日志",
    ok: "读取正常", unsupported: "固件不支持日志，请更新对应固件",
    unavailable: "暂时无法读取，结果可能不完整", storage_error: "日志存储异常，已保留可读取记录",
    pending: "待落盘", dropped: "丢弃数量", overwritten: "部分历史已覆盖",
    restricted: "连续异常后的诊断维护状态", details: "现场详情",
    note: "按设备分别显示启动编号与启动后时间。待落盘记录可能因断电丢失；“暂无日志”不代表设备未发生故障。",
    incomplete: "已导出部分结果，文件中已标注读取异常或历史覆盖。",
    exported: "已导出完整读取结果。", firmware: "固件版本", boot: "启动", repeat: "次数",
    event: "事件", exportFailed: "导出失败，请重新连接后重试。",
    events: ["未知事件", "处理器异常", "致命错误", "角色启动失败", "板间通信异常", "USB 异常", "RF 异常", "链路恢复", "日志缓冲溢出"],
  },
  en: {
    title: "Device logs", refresh: "Refresh", loading: "Reading…", export: "Export",
    source: "Source", category: "Category", severity: "Severity", all: "All", controller: "Main controller",
    fault: "Fatal exception", link: "Link fault", recovery: "Recovery", warning: "Warning", error: "Error", fatal: "Fatal",
    disconnected: "Connect a device to read logs.", empty: "No matching records",
    ok: "Read successfully", unsupported: "Logging unsupported; update this firmware",
    unavailable: "Temporarily unavailable; results may be incomplete", storage_error: "Storage error; readable records retained",
    pending: "Pending persistence", dropped: "Dropped", overwritten: "Some history has been overwritten",
    restricted: "Diagnostic maintenance after repeated faults", details: "Fault details",
    note: "Boot numbers and uptime are shown separately for each device. Pending records may be lost on power removal. An empty log does not prove no faults occurred.",
    incomplete: "Partial results exported; read errors or overwritten history are marked in the file.",
    exported: "Complete read results exported.", firmware: "Firmware", boot: "Boot", repeat: "Count",
    event: "Event", exportFailed: "Export failed. Reconnect and try again.",
    events: ["Unknown event", "CPU exception", "Fatal error", "Role startup failure", "BoardLink fault", "USB fault", "RF fault", "Link recovered", "Log buffer overflow"],
  },
};
const firmware = (v: number) => `${v >>> 16}.${(v >>> 8) & 255}.${v & 255}`;
function LogFilter({ label, value, items, disabled, onValueChange }: {
  label: string; value: string; items: { value: string; label: string }[];
  disabled: boolean; onValueChange: (value: string) => void;
}) {
  const collection = createListCollection({ items });
  return <SelectRoot collection={collection} value={[value]} disabled={disabled}
    onValueChange={details => { if (details.value[0]) onValueChange(details.value[0]); }}
    size="sm" width="full" minW={0}>
    <SelectLabel>{label}</SelectLabel>
    <SelectTrigger><SelectValueText /></SelectTrigger>
    <SelectContent>{collection.items.map(item => <SelectItem key={item.value} item={item}>{item.label}</SelectItem>)}</SelectContent>
  </SelectRoot>;
}
export function ViewLogsContent() {
  const { currentLanguage } = useLanguage();
  const zh = currentLanguage === 'zh';
  const t = labels[currentLanguage];
  const { deviceState, dataIsReady, fetchDeviceLogsPage, clearDeviceLogs, flushDeferredConfig } = useGamepadFields(
    "deviceState", "dataIsReady", "fetchDeviceLogsPage", "clearDeviceLogs", "flushDeferredConfig");
  const [source, setSource] = useState<LogSource>('stm32');
  const [category, setCategory] = useState(0);
  const [severity, setSeverity] = useState(0);
  const [results, setResults] = useState<Partial<Record<LogSource, FaultLogPage>>>({});
  const [progress, setProgress] = useState<FaultLogPage | null>(null);
  const [message, setMessage] = useState('');
  const [clearConfirm, setClearConfirm] = useState(false);
  const [leaveConfirm, setLeaveConfirm] = useState(false);
  const resolveLeave = useRef<((leave: boolean) => void) | null>(null);
  const generation = useRef(0);
  const scrollRef = useRef<HTMLDivElement>(null);
  const operation = useLogOperation(state => state.operation);
  const setOperation = useLogOperation(state => state.setOperation);
  const busy = operation !== null;
  const connected = deviceState === DeviceTransportState.CONNECTED && dataIsReady;
  const batteryLabel = zh ? '电池测试' : 'Battery test';
  const navigation = [
    { value: 'stm32' as const, label: t.controller, icon: LuCpu },
    { value: 'tx' as const, label: 'TX', icon: LuRadio },
    { value: 'battery' as const, label: batteryLabel, icon: LuBatteryMedium },
  ];
  const sourceLabel = navigation.find(item => item.value === source)!.label;
  const result = results[source];
  const page = result && { ...result, items: result.items.filter(record =>
    (!category || record.category === category) && (!severity || record.severity === severity)) };
  const percent = progress ? logReadPercent(progress) : null;

  function resolveNavigation(leave: boolean) {
    if (leave) { ++generation.current; setOperation(null); }
    setLeaveConfirm(false);
    const resolve = resolveLeave.current; resolveLeave.current = null; resolve?.(leave);
  }
  useEffect(() => {
    if (!connected) {
      ++generation.current; setResults({}); setProgress(null); setOperation(null);
      setClearConfirm(false); setLeaveConfirm(false);
      resolveLeave.current?.(false); resolveLeave.current = null;
    }
  }, [connected, setOperation]);
  useEffect(() => {
    const guard = async () => {
      const active = useLogOperation.getState().operation;
      if (!active) return true;
      if (active === 'clear') return false;
      if (resolveLeave.current) return false;
      setLeaveConfirm(true);
      return new Promise<boolean>(resolve => { resolveLeave.current = resolve; });
    };
    const unload = (event: BeforeUnloadEvent) => {
      if (useLogOperation.getState().operation) { event.preventDefault(); event.returnValue = ''; }
    };
    const link = (event: MouseEvent) => {
      const anchor = (event.target as Element).closest?.('a[href]') as HTMLAnchorElement | null;
      if (!anchor || !useLogOperation.getState().operation || anchor.target === '_blank' || anchor.download) return;
      event.preventDefault(); event.stopPropagation();
      void guard().then(leave => { if (leave) window.location.assign(anchor.href); });
    };
    navigationEvents.addListener(guard);
    window.addEventListener('beforeunload', unload);
    document.addEventListener('click', link, true);
    return () => {
      ++generation.current; setOperation(null);
      resolveLeave.current?.(false); resolveLeave.current = null;
      navigationEvents.removeListener(guard);
      window.removeEventListener('beforeunload', unload);
      document.removeEventListener('click', link, true);
    };
  }, [setOperation]);
  useEffect(() => { scrollRef.current?.scrollTo(0, 0); }, [source, category, severity, result]);

  async function read() {
    if (!connected || useLogOperation.getState().operation) return;
    const token = ++generation.current;
    setOperation('read'); setProgress(null); setMessage('');
    // A fixed full-source snapshot supports local filters and honest progress.
    const next = await collectFaultLogs(fetchDeviceLogsPage, { source },
      () => generation.current !== token,
      value => { if (generation.current === token) setProgress(value); });
    if (generation.current !== token) return;
    setResults(previous => ({ ...previous, [source]: next })); setOperation(null);
  }
  async function clearLogs() {
    setClearConfirm(false);
    if (!connected || useLogOperation.getState().operation) return;
    const token = ++generation.current;
    setOperation('clear'); setMessage('');
    try {
      await flushDeferredConfig(() => clearDeviceLogs(source));
      if (generation.current !== token) return;
      setResults(previous => ({ ...previous, [source]: undefined })); setProgress(null);
      setMessage(zh ? '此来源的日志已清空。' : 'Logs for this source have been cleared.');
    } catch (error) {
      if (generation.current !== token) return;
      // Never leave a pre-clear snapshot looking current after a partial erase.
      setResults(previous => ({ ...previous, [source]: undefined }));
      setMessage((zh ? '清空未完成，请重新读取确认设备状态。' : 'Clear was not completed. Read again to check device state. ')
        + deviceDisplayText(error instanceof Error ? error.message : String(error), currentLanguage));
    } finally { if (generation.current === token) setOperation(null); }
  }
  function download(format: 'json' | 'txt') {
    if (!page || busy) return;
    const output = logExport([page], { source, category, severity });
    const text = format === 'json' ? JSON.stringify(output, null, 2) : logExportText(output);
    const url = URL.createObjectURL(new Blob([text], { type: format === 'json' ? 'application/json;charset=utf-8' : 'text/plain;charset=utf-8' }));
    const a = document.createElement('a'); a.href = url;
    a.download = `XORA-device-logs-${new Date().toISOString().replace(/[:.]/g, '-')}.${format}`;
    a.click(); setTimeout(() => URL.revokeObjectURL(url), 1000);
    setMessage(output.complete ? t.exported : t.incomplete);
  }
  return <>
    <Flex p={{ base: 3, md: '18px' }} width="full" height="100%" flex={1} minW={0} minH={0}
      overflow="hidden" direction={{ base: 'column', md: 'row' }} fontSize="sm" letterSpacing="normal">
      <Flex as="aside" data-testid="log-sidebar" direction="column" gap={3} flexShrink={0}
        width={{ base: 'full', md: SETTINGS_SIDEBAR_WIDTH }} minW={{ md: SETTINGS_SIDEBAR_WIDTH }}
        borderRightWidth={{ base: 0, md: '1px' }} borderColor="border" pr={{ md: '18px' }} pb={{ base: 3, md: 0 }}>
        <Heading fontSize="md" fontWeight="semibold">{t.title}</Heading>
        <Flex as="nav" aria-label={t.source} direction={{ base: 'row', md: 'column' }} gap={1} minW={0}>
          {navigation.map(item => <Button key={item.value} variant="ghost" colorPalette="green" disabled={busy}
            h="38px" px={3} gap={2.5} minW={0} flex={{ base: '1 1 0', md: 'none' }} w={{ md: 'full' }}
            justifyContent={{ base: 'center', md: 'flex-start' }} borderWidth="1px" borderRadius="md"
            borderColor={item.value === source ? 'green.muted' : 'transparent'}
            bg={item.value === source ? 'green.subtle' : 'transparent'} color={item.value === source ? 'green.fg' : 'fg.muted'}
            _hover={{ bg: 'bg.muted' }} aria-current={item.value === source ? 'page' : undefined}
            onClick={() => { setSource(item.value); setMessage(''); }}>
            <Box as={item.icon} boxSize="15px" flexShrink={0} display={{ base: 'none', sm: 'block' }} />
            <Text as="span" truncate>{item.label}</Text>
          </Button>)}
        </Flex>
      </Flex>
      <VStack align="stretch" gap={3} flex={1} minW={0} minH={0} pl={{ md: 6 }}>
        <Flex justify="space-between" align="center" gap={3} flexWrap="wrap" flexShrink={0}>
          <Heading as="h1" fontSize="xl">{sourceLabel}</Heading>
          <HStack gap={2} flexWrap="wrap">
            <PanelActionButton accent disabled={!connected || busy} onClick={() => void read()}><LuRefreshCw />{t.refresh}</PanelActionButton>
            <PanelActionButton disabled={!connected || busy} onClick={() => setClearConfirm(true)}><LuTrash2 />{zh ? '清空日志' : 'Clear logs'}</PanelActionButton>
            <PanelActionButton disabled={!page || busy} onClick={() => download('json')}><LuFileJson />{t.export} JSON</PanelActionButton>
            <PanelActionButton disabled={!page || busy} onClick={() => download('txt')}><LuFileText />{t.export} TXT</PanelActionButton>
          </HStack>
        </Flex>
        <Text color="fg.muted" fontSize="xs" lineHeight="1.6" lineClamp={2} title={t.note} flexShrink={0}>{t.note}</Text>
        <Grid templateColumns="repeat(2, minmax(0, 1fr))" gap={3} maxW="360px" flexShrink={0}>
          <LogFilter label={t.category} disabled={busy} value={String(category)} onValueChange={value => setCategory(Number(value))}
            items={[t.all, t.fault, t.link, t.recovery, batteryLabel].map((label, value) => ({ value: String(value), label }))} />
          <LogFilter label={t.severity} disabled={busy} value={String(severity)} onValueChange={value => setSeverity(Number(value))}
            items={[{ value: '0', label: t.all }, { value: '2', label: t.warning }, { value: '3', label: t.error }, { value: '4', label: t.fatal }]} />
        </Grid>
        {message && <Text role="status" fontSize="xs" flexShrink={0}>{message}</Text>}
        <Flex as="section" data-testid="log-card" aria-label={sourceLabel} aria-busy={busy}
          direction="column" flex={1} minH={0} minW={0} borderWidth="1px" borderRadius="lg" overflow="hidden" bg="bg.panel">
          {busy || !page ? <Flex flex={1} minH={0} align="center" justify="center" p={6}>
            <VStack width="full" maxW="360px" gap={5}>
              <Flex boxSize="44px" align="center" justify="center" borderRadius="xl" borderWidth="1px" borderColor="green.muted" bg="green.subtle" color="green.fg"><LuScrollText size={20} /></Flex>
              <VStack gap={1.5}>
                <Text fontSize="md" fontWeight="medium">{busy ? operation === 'clear' ? (zh ? '正在清空日志' : 'Clearing logs') : (zh ? '正在读取日志' : 'Reading logs') : (zh ? '日志尚未读取' : 'Logs have not been read')}</Text>
                <Text fontSize="xs" color="fg.muted">{busy ? operation === 'clear' ? (zh ? '等待设备清空并校验存储区域…' : 'Waiting for device erase and verification…') : (zh ? '读取完成后显示完整列表。' : 'The list will appear when reading finishes.') : connected ? sourceLabel : t.disconnected}</Text>
              </VStack>
              {busy ? <Box width="full">
                <Progress.Root value={operation === 'clear' ? null : percent} max={100} colorPalette="green" size="xs"
                  aria-label={zh ? '日志读取进度' : 'Log read progress'}>
                  <Progress.Track h="4px" borderRadius="full" bg="bg.emphasized"><Progress.Range borderRadius="full" bg="green.400" transition="width 180ms ease-out" /></Progress.Track>
                </Progress.Root>
                <Flex justify="space-between" mt={3} fontSize="10px" color="fg.muted" fontVariantNumeric="tabular-nums">
                  <Text>{operation === 'clear' ? sourceLabel : zh ? `已读取 ${progress?.items.length ?? 0} 条记录` : `${progress?.items.length ?? 0} records read`}</Text>
                  <Text color="green.fg">{operation === 'clear' || percent === null ? '···' : `${percent}%`}</Text>
                </Flex>
              </Box> : <PanelActionButton accent disabled={!connected} onClick={() => void read()}><LuPlay />{zh ? '开始读取日志' : 'Start reading logs'}</PanelActionButton>}
            </VStack>
          </Flex> : <>
            <Box px={{ base: 3, md: 4 }} py={3} borderBottomWidth="1px" lineHeight="1.4" flexShrink={0}>
              <Flex align="center" justify="space-between" gap={2} flexWrap="wrap">
                <HStack gap={2.5}><Box boxSize="6px" borderRadius="full" bg={page.status === 'ok' ? 'green.400' : 'orange.400'} />
                  <Heading fontSize="sm" fontWeight="semibold">{zh ? '日志记录' : 'Log records'}</Heading>
                  <Text fontSize="10px" color="fg.muted">{page.items.length} / {result?.items.length} {zh ? '条记录' : 'records'}</Text></HStack>
                <Badge colorPalette={page.status === 'ok' ? 'green' : 'orange'} variant="subtle" fontSize="10px" whiteSpace="normal">{t[page.status]}</Badge>
              </Flex>
              <HStack flexWrap="wrap" gapX={4} gapY={1} mt={2} fontSize="10px" color="fg.muted">
                <Text>{t.firmware} {firmware(page.firmware)}</Text><Text>{t.pending} {page.pending}</Text><Text>{t.dropped} {page.dropped}</Text>
              </HStack>
              {page.overwritten && <Text mt={2} fontSize="11px" color="orange.fg">{t.overwritten}</Text>}
              {page.restricted && <Text mt={2} fontSize="11px" color="orange.fg">{t.restricted}</Text>}
              {page.errorMessage && <Text mt={2} fontSize="11px" overflowWrap="anywhere">{deviceDisplayText(page.errorMessage, currentLanguage)}</Text>}
            </Box>
            <Box ref={scrollRef} data-testid="log-scroll-region" flex={1} minH={0} minW={0} overflowY="auto" overflowX="hidden"
              tabIndex={0} aria-label={zh ? '日志内容' : 'Log content'} css={{ scrollbarGutter: 'stable', scrollbarWidth: 'thin' }}>
              {page.source === 'battery' && page.items.length > 0 && <VStack align="stretch" gap={3} p={{ base: 3, md: 4 }}><BatteryTestLogSummary page={page} language={currentLanguage} /></VStack>}
              {!page.items.length && page.status === 'ok' && <Text color="fg.muted" px={4} py={6}>{t.empty}</Text>}
              {page.items.length > 0 && <DeviceLogList page={page} language={currentLanguage} scrollRef={scrollRef} />}
            </Box>
          </>}
        </Flex>
      </VStack>
    </Flex>
    <LogConfirmDialog open={leaveConfirm} title={zh ? '中断日志读取？' : 'Interrupt log reading?'}
      description={zh ? '切换页面会中断当前读取，尚未完成的结果不会保留。是否继续？' : 'Leaving will interrupt this read and discard its unfinished results. Continue?'}
      cancel={zh ? '继续读取' : 'Keep reading'} confirm={zh ? '中断并离开' : 'Interrupt and leave'}
      onCancel={() => resolveNavigation(false)} onConfirm={() => resolveNavigation(true)} />
    <LogConfirmDialog open={clearConfirm} danger title={zh ? `清空${sourceLabel}日志？` : `Clear ${sourceLabel} logs?`}
      description={zh ? `将永久删除${sourceLabel}的全部已存储及待落盘日志，包括当前筛选隐藏的记录。此操作无法撤销，建议先导出。` : `Permanently delete all stored and pending ${sourceLabel} logs, including records hidden by filters. This cannot be undone. Export first if needed.`}
      cancel={zh ? '取消' : 'Cancel'} confirm={zh ? '确认清空' : 'Clear logs'} onCancel={() => setClearConfirm(false)} onConfirm={() => void clearLogs()} />
  </>;
}
export default ViewLogsContent;
