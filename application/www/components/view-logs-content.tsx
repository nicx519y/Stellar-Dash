"use client";

import { useCallback, useEffect, useRef, useState } from "react";
import { Box, Button, Card, Heading, HStack, Text, VStack, Badge } from "@chakra-ui/react";
import { useGamepadFields } from "@/contexts/gamepad-config-context";
import { useLanguage } from "@/contexts/language-context";
import { DeviceTransportState } from "@/lib/device-transport";
import {
  collectFaultLogs, failedLogPage, logExport, logExportText, mergeLogPages,
  type FaultLogPage, type LogSource,
} from "@/lib/device-fault-logs";

const labels = {
  zh: {
    title: "设备日志", refresh: "刷新", loading: "读取中…", export: "导出",
    source: "来源", category: "类别", severity: "级别", all: "全部",
    fault: "致命异常", link: "异常链路", recovery: "恢复摘要", warning: "警告", error: "错误", fatal: "致命",
    disconnected: "连接设备后可读取日志。", empty: "暂无符合条件的日志",
    ok: "读取正常", unsupported: "固件不支持日志，请更新对应固件",
    unavailable: "暂时无法读取，结果可能不完整", storage_error: "日志存储异常，已保留可读取记录",
    pending: "待落盘", dropped: "丢弃数量", overwritten: "部分历史已覆盖",
    restricted: "连续异常后的诊断维护状态", more: "加载更多", details: "现场详情",
    note: "按设备分别显示启动编号与启动后时间。待落盘记录可能因断电丢失；“暂无日志”不代表设备未发生故障。",
    incomplete: "已导出部分结果，文件中已标注读取异常或历史覆盖。",
    exported: "已导出完整读取结果。", firmware: "固件版本", boot: "启动", repeat: "次数",
    event: "事件", exportFailed: "导出失败，请重新连接后重试。",
    events: ["未知事件", "处理器异常", "致命错误", "角色启动失败", "板间通信异常", "USB 异常", "RF 异常", "链路恢复", "日志缓冲溢出"],
  },
  en: {
    title: "Device logs", refresh: "Refresh", loading: "Reading…", export: "Export",
    source: "Source", category: "Category", severity: "Severity", all: "All",
    fault: "Fatal exception", link: "Link fault", recovery: "Recovery", warning: "Warning", error: "Error", fatal: "Fatal",
    disconnected: "Connect a device to read logs.", empty: "No matching records",
    ok: "Read successfully", unsupported: "Logging unsupported; update this firmware",
    unavailable: "Temporarily unavailable; results may be incomplete", storage_error: "Storage error; readable records retained",
    pending: "Pending persistence", dropped: "Dropped", overwritten: "Some history has been overwritten",
    restricted: "Diagnostic maintenance after repeated faults", more: "Load more", details: "Fault details",
    note: "Boot numbers and uptime are shown separately for each device. Pending records may be lost on power removal. An empty log does not prove no faults occurred.",
    incomplete: "Partial results exported; read errors or overwritten history are marked in the file.",
    exported: "Complete read results exported.", firmware: "Firmware", boot: "Boot", repeat: "Count",
    event: "Event", exportFailed: "Export failed. Reconnect and try again.",
    events: ["Unknown event", "CPU exception", "Fatal error", "Role startup failure", "BoardLink fault", "USB fault", "RF fault", "Link recovered", "Log buffer overflow"],
  },
};
const firmware = (v: number) => `${v >>> 16}.${(v >>> 8) & 255}.${v & 255}`;
export function ViewLogsContent() {
  const { currentLanguage } = useLanguage();
  const t = labels[currentLanguage];
  const { deviceState, dataIsReady, fetchDeviceLogsPage } = useGamepadFields("deviceState", "dataIsReady", "fetchDeviceLogsPage");
  const [source, setSource] = useState<"all" | LogSource>("all");
  const [category, setCategory] = useState(0);
  const [severity, setSeverity] = useState(0);
  const [pages, setPages] = useState<FaultLogPage[]>([]);
  const [busy, setBusy] = useState(false);
  const [message, setMessage] = useState("");
  const generation = useRef(0);
  const invalidateReads = useCallback(() => { ++generation.current; }, []);
  const connected = deviceState === DeviceTransportState.CONNECTED && dataIsReady;
  const sources: LogSource[] = source === "all" ? ["stm32", "tx"] : [source];

  async function refresh() {
    const token = ++generation.current;
    setBusy(true); setMessage("");
    const results: FaultLogPage[] = [];
    for (const selected of sources) {
      if (generation.current !== token) return;
      try { results.push(await fetchDeviceLogsPage({ source: selected, category, severity })); }
      catch (error) { results.push(failedLogPage(selected, error)); }
    }
    if (generation.current === token) { setPages(results); setBusy(false); }
  }
  useEffect(() => {
    setPages([]); setMessage(""); setBusy(false);
    if (connected) void refresh();
    return invalidateReads;
    // Only connection/filter changes start a read; there is no polling timer.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [connected, source, category, severity, invalidateReads]);

  async function more(page: FaultLogPage) {
    const token = generation.current;
    setBusy(true);
    try {
      const next = await fetchDeviceLogsPage({ source: page.source, category, severity,
        upper: page.upper, oldest: page.oldest, before: page.nextBefore });
      const merged = mergeLogPages(page, next);
      if (generation.current === token) setPages(current => current.map(p => p.source === page.source ? merged : p));
    } catch {
      if (generation.current === token) setPages(current => current.map(p => p.source === page.source ? { ...p, status: "unavailable" } : p));
    } finally { if (generation.current === token) setBusy(false); }
  }
  async function download(format: "json" | "txt") {
    const token = generation.current;
    setBusy(true); setMessage("");
    try {
      const results: FaultLogPage[] = [];
      for (const selected of sources) {
        if (generation.current !== token) return;
        results.push(await collectFaultLogs(fetchDeviceLogsPage, { source: selected, category, severity },
          () => generation.current !== token));
      }
      if (generation.current !== token) return;
      const output = logExport(results, { source, category, severity });
      const text = format === "json" ? JSON.stringify(output, null, 2) : logExportText(output);
      const url = URL.createObjectURL(new Blob([text], { type: format === "json" ? "application/json;charset=utf-8" : "text/plain;charset=utf-8" }));
      const a = document.createElement("a"); a.href = url;
      a.download = `XORA-device-logs-${new Date().toISOString().replace(/[:.]/g, "-")}.${format}`;
      a.click(); setTimeout(() => URL.revokeObjectURL(url), 1000);
      setMessage(output.complete ? t.exported : t.incomplete);
    } catch { if (generation.current === token) setMessage(t.exportFailed); }
    finally { if (generation.current === token) setBusy(false); }
  }
  return (
    <Box p={{ base: 3, md: 6 }} width="100%" maxW="1100px" minW={0}>
      <VStack align="stretch" gap={4}>
        <HStack justify="space-between" flexWrap="wrap">
          <Heading size="lg">{t.title}</Heading>
          <HStack flexWrap="wrap">
            <Button disabled={!connected || busy} onClick={() => void refresh()}>{busy ? t.loading : t.refresh}</Button>
            <Button disabled={!connected || busy} onClick={() => void download("json")}>{t.export} JSON</Button>
            <Button disabled={!connected || busy} onClick={() => void download("txt")}>{t.export} TXT</Button>
          </HStack>
        </HStack>
        <Text color="fg.muted" fontSize="sm">{t.note}</Text>
        <HStack gap={4} flexWrap="wrap">
          <label>{t.source} <select aria-label={t.source} disabled={busy} value={source} onChange={e => setSource(e.target.value as typeof source)}>
            <option value="all">{t.all}</option><option value="stm32">STM32</option><option value="tx">CH585 TX</option>
          </select></label>
          <label>{t.category} <select aria-label={t.category} disabled={busy} value={category} onChange={e => setCategory(Number(e.target.value))}>
            <option value={0}>{t.all}</option><option value={1}>{t.fault}</option><option value={2}>{t.link}</option><option value={3}>{t.recovery}</option>
          </select></label>
          <label>{t.severity} <select aria-label={t.severity} disabled={busy} value={severity} onChange={e => setSeverity(Number(e.target.value))}>
            <option value={0}>{t.all}</option><option value={2}>{t.warning}</option><option value={3}>{t.error}</option><option value={4}>{t.fatal}</option>
          </select></label>
        </HStack>
        {!connected && <Text role="status">{t.disconnected}</Text>}
        {message && <Text role="status">{message}</Text>}
        {pages.map(page => <Card.Root key={page.source}>
          <Card.Header>
            <HStack justify="space-between" flexWrap="wrap">
              <Heading size="md">{page.source === "stm32" ? "STM32" : "CH585 TX"}</Heading>
              <Badge colorPalette={page.status === "ok" ? "green" : "orange"} whiteSpace="normal">{t[page.status]}</Badge>
            </HStack>
            <Text fontSize="sm">{t.firmware}: {firmware(page.firmware)} · {t.pending}: {page.pending} · {t.dropped}: {page.dropped}</Text>
            {page.overwritten && <Text color="orange.500">{t.overwritten}</Text>}
            {page.restricted && <Text color="orange.500">{t.restricted}</Text>}
            {page.errorMessage && <Text fontSize="sm" overflowWrap="anywhere">{page.errorMessage}</Text>}
          </Card.Header>
          <Card.Body>
            <VStack align="stretch" gap={3}>
              {!page.items.length && page.status === "ok" && <Text color="fg.muted">{t.empty}</Text>}
              {page.items.map((r, index) => <Box key={`${r.boot}-${r.sequence}-${index}`} borderWidth="1px" rounded="md" p={3}>
                <HStack justify="space-between" flexWrap="wrap">
                  <Text fontWeight="semibold">{t.events[r.event] ?? `${t.event} ${r.event}`}</Text>
                  <HStack>
                    <Badge colorPalette={r.severity === 4 ? "red" : r.severity === 3 ? "orange" : "yellow"}>{r.severity === 4 ? t.fatal : r.severity === 3 ? t.error : t.warning}</Badge>
                    {r.pending && <Badge>{t.pending}</Badge>}
                  </HStack>
                </HStack>
                <Text fontSize="sm" color="fg.muted">{t.boot} #{r.boot} · +{(r.uptimeMs / 1000).toFixed(3)} s · {t.repeat}: {r.repeat}</Text>
                <details><summary>{t.details}</summary>
                  <Text fontFamily="mono" fontSize="sm" overflowWrap="anywhere">seq={r.sequence} · firmware={firmware(r.firmware)} · event={r.event}<br />
                    {r.data.map((v, i) => `[${i}]=0x${v.toString(16).padStart(8, "0")}`).join("  ")}</Text>
                </details>
              </Box>)}
              {page.more && <Button disabled={!connected || busy || page.status === "unavailable"} onClick={() => void more(page)}>{t.more}</Button>}
            </VStack>
          </Card.Body>
        </Card.Root>)}
      </VStack>
    </Box>
  );
}
export default ViewLogsContent;
