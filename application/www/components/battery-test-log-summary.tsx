import { Badge, Box, Flex, HStack, SimpleGrid, Text } from '@chakra-ui/react';
import { batterySessions, type BatteryCycleEnd, type BatteryCycleIssue } from '@/lib/battery-test-logs';
import type { FaultLogPage } from '@/lib/device-fault-logs';
import { BatteryTrendChart } from './battery-trend-chart';
import { ChargeTestLogSummary } from './charge-test-log-summary';
import { decodeBatteryRecord } from '@/lib/battery-test-logs';

export function BatteryTestLogSummary({ page, language }: { page: FaultLogPage; language: 'zh' | 'en' }) {
  const zh = language === 'zh';
  const cycleReasons: Record<BatteryCycleEnd, string> = zh ? {
    protection: '到达电池保护', external_power: '插电中断', charging: '充电中断', manual: '手动结束',
    timeout: '达到时间上限', reboot: '重启中断', storage_failure: '存储失败', unknown: '终点未知', ongoing: '仍在放电',
  } : { protection: 'Battery protection reached', external_power: 'External power interrupted discharge',
    charging: 'Charging interrupted discharge', manual: 'Manually ended', timeout: 'Time limit reached',
    reboot: 'Reboot interrupted discharge', storage_failure: 'Storage failure', unknown: 'Unknown endpoint', ongoing: 'Discharging' };
  const issueLabels: Record<BatteryCycleIssue, string> = zh ? {
    partial_log: '日志读取不完整', missing_records: '记录缺失或丢弃', pending_records: '含待落盘记录',
    configuration_changed: '放电期间配置变化', sample_gap: '采样存在缺口', invalid_sample: '电源采样无效或过期',
    reboot: '重启导致终点未知', unknown_start: '缺少测试起始记录',
  } : { partial_log: 'Incomplete log read', missing_records: 'Missing or dropped records', pending_records: 'Pending persistence',
    configuration_changed: 'Configuration changed during discharge', sample_gap: 'Sample gap',
    invalid_sample: 'Invalid or stale power sample', reboot: 'Endpoint unknown after reboot', unknown_start: 'Test start record missing' };
  return <><ChargeTestLogSummary page={page} language={language} />
    {page.items.some(r => decodeBatteryRecord(r).unsupported) && <Text fontSize="11px" color="orange.fg">{zh ? '部分记录格式不受支持，请更新 WebConfig；原始数据仍可导出。' : 'Some record formats are unsupported. Update WebConfig; raw records can still be exported.'}</Text>}
    {batterySessions(page.items, page).map(session => {
    const end = session.end?.data;
    const knownDuration = end && end[2] !== 4;
    const incomplete = session.incomplete || page.more;
    const reasons = zh ? ['未知', '恢复完成', '手动结束', '达到 24 小时', '重启中断', '存储失败'] :
      ['Unknown', 'Recovery completed', 'Manually ended', '24 hour limit', 'Reboot interrupted', 'Storage failure'];
    const hours = (ms: number | null) => ms === null ? '—' : `${(ms / 3600000).toFixed(2)} h`;
    const stats = [
      [zh ? '会话电池供电合计（含多段）' : 'Session battery time (all segments)', knownDuration ? hours(end[4]) : '—'],
      [zh ? '普通休眠' : 'Ordinary sleep', knownDuration ? `${(end[5] / 1000).toFixed(1)} s` : '—'],
      [zh ? '记录存储暂停' : 'Recorded storage pauses', knownDuration ? `${(end[6] / 1000).toFixed(3)} s` : '—'],
      [zh ? '放电段数' : 'Discharge segments', String(session.cycles.length)],
    ];
    return <Box key={session.id} borderWidth="1px" borderRadius="lg" bg="bg.subtle" p={{ base: 3, md: 4 }} letterSpacing="normal" lineHeight="1.5">
      <Flex align="center" justify="space-between" gap={2} flexWrap="wrap" mb={4}>
        <HStack gap={2}><Text fontSize="13px" fontWeight="semibold">{zh ? '续航测试' : 'Discharge test'}</Text>
          <Text fontFamily="mono" fontSize="11px" color="fg.muted">#{String(session.id).padStart(3, '0')}</Text>
          <Badge colorPalette={incomplete ? 'orange' : 'green'} variant="subtle" fontSize="10px">{incomplete ? zh ? '部分记录' : 'Partial records' : zh ? '记录完整' : 'Complete records'}</Badge>
        </HStack>
        {end && <Text fontSize="11px" color="fg.muted">{zh ? '结束原因' : 'End reason'} · {reasons[end[2]] ?? reasons[0]}</Text>}
      </Flex>
      <SimpleGrid columns={{ base: 2, md: 4 }} gap={3} mb={5}>
        {stats.map(([label, value]) => <Box key={label} borderLeftWidth="2px" borderColor="border.muted" pl={3}>
          <Text fontSize="10px" color="fg.muted" mb={1}>{label}</Text>
          <Text fontFamily="mono" fontSize={{ base: '13px', md: '15px' }} fontWeight="medium" lineHeight="1.4">{value}</Text>
        </Box>)}
      </SimpleGrid>
      {renderCycles()}
      <BatteryTrendChart trend={session.trend} start={session.start} language={language} />
      <Box mt={3} pt={3} borderTopWidth="1px" fontSize="11px" color="fg.muted" lineHeight="1.6">
        <Text>{page.more && page.status === 'ok'
          ? (zh ? '正在自动读取其余记录…' : 'Reading remaining records automatically…')
          : page.more || page.status !== 'ok'
            ? (zh ? '仅显示成功读取的记录，可刷新重试。' : 'Showing readable records. Refresh to retry.')
            : (zh ? '已读取当前筛选下的全部记录。' : 'All records matching the current filters have been read.')}
          {incomplete && <Box as="span" color="orange.fg" ml={1}>{zh ? '记录未结束、不完整或尚未加载完整。' : 'Unfinished, incomplete, or not fully loaded.'}</Box>}
        </Text>
        {end?.[2] === 4 && <Text color="orange.fg">{zh ? '断电或重启后的缺失时段未知，请依据已保存采样分析。' : 'The missing interval after power loss or reboot is unknown. Use the saved samples.'}</Text>}
      </Box>
    </Box>;

    function renderCycles() {
      return <Box mb={5}>
        <Text fontSize="12px" fontWeight="semibold" mb={2}>{zh ? '连续放电周期' : 'Continuous discharge cycles'}</Text>
        {!session.cycles.length && <Text fontSize="11px" color="orange.fg">{zh ? '暂无可确认起点的放电段，请加载完整日志。' : 'No discharge segment with a known start. Load the full log.'}</Text>}
        {session.cycles.map(cycle => <Box key={cycle.id} borderWidth="1px" borderRadius="md" p={3} mb={2}>
          <Flex gap={2} align="center" flexWrap="wrap" mb={2}>
            <Text fontSize="12px">{zh ? '放电段' : 'Segment'} #{cycle.id}</Text>
            <Badge colorPalette={cycle.status === 'valid' ? 'green' : cycle.status === 'recording' ? 'blue' : 'orange'}>
              {cycle.status === 'valid' ? zh ? '有效连续放电' : 'Valid continuous discharge' :
                cycle.status === 'recording' ? zh ? '进行中' : 'In progress' :
                  cycle.status === 'interrupted' ? zh ? '未完成放电' : 'Discharge interrupted' : zh ? '无法验证' : 'Unverified'}
            </Badge>
            <Text fontSize="11px" color="fg.muted">{cycleReasons[cycle.endReason]}</Text>
          </Flex>
          <SimpleGrid columns={{ base: 1, sm: 3 }} gap={2} fontSize="11px">
            <Text>{zh ? '已观察时长' : 'Observed duration'} · {hours(cycle.elapsedMs)}</Text>
            <Text>{zh ? '起点 / 终点 SOC' : 'Start / end SOC'} · {cycle.startSocPercent?.toFixed(1) ?? '—'}% / {cycle.endSocPercent?.toFixed(1) ?? '—'}%</Text>
            <Text>{zh ? '有效放电至保护' : 'Verified time to protection'} · {hours(cycle.enduranceMs)}</Text>
          </SimpleGrid>
          {cycle.issues.length > 0 && <Text mt={2} fontSize="11px" color="orange.fg">{cycle.issues.map(reason => issueLabels[reason]).join(zh ? '；' : '; ')}</Text>}
        </Box>)}
        <Text fontSize="11px" color="fg.muted">{zh ? '每次插电结束当前放电段；保护后的充电恢复不计入放电成绩。有效成绩仅覆盖所示起点 SOC 至保护，不代表满电续航。' : 'External power ends each segment. Charging recovery is excluded. Verified results cover the shown starting SOC to protection, and do not establish full-charge battery life.'}</Text>
      </Box>;
    }
  })}</>;
}
