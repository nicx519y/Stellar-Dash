import { Badge, Box, Flex, HStack, SimpleGrid, Text } from '@chakra-ui/react';
import { chargeInterval, chargeMilestone, chargeSessions, type ChargeIssue } from '@/lib/charge-test-logs';
import type { FaultLogPage } from '@/lib/device-fault-logs';
import { ChargeTrendChart } from './charge-trend-chart';

export function ChargeTestLogSummary({ page, language }: { page: FaultLogPage; language: 'zh' | 'en' }) {
  const zh = language === 'zh';
  const hours = (ms: number | null | undefined) => ms == null ? '—' : `${(ms / 3600000).toFixed(2)} h`;
  const soc = (value: number | null | undefined) => value == null ? '—' : `${value.toFixed(1)}%`;
  const voltage = (value: number | null) => value === null ? '—' : `${(value / 1000).toFixed(2)} V`;
  const reasons = zh ? ['终点未知', '确认充满', '手动结束', '达到 24 小时', '重启中断', '存储失败', '拔电中断'] :
    ['Unknown endpoint', 'Full confirmed', 'Manually ended', '24 hour limit', 'Reboot interrupted', 'Storage failure', 'Unplugged'];
  const issues: Record<ChargeIssue, string> = zh ? {
    partial_log: '日志读取不完整', missing_records: '记录缺失或丢弃', pending_records: '含待落盘记录', configuration_changed: '负载配置变化',
    sample_gap: '采样缺口', invalid_sample: '采样无效或过期', reboot: '重启后终点未知', unknown_start: '缺少测试起点', supply_changed: '输入供电档位变化', unsupported: '含不支持的记录格式',
  } : { partial_log: 'Partial log read', missing_records: 'Missing or dropped records', pending_records: 'Pending persistence', configuration_changed: 'Load configuration changed',
    sample_gap: 'Sample gap', invalid_sample: 'Invalid or stale samples', reboot: 'Endpoint unknown after reboot', unknown_start: 'Missing test start', supply_changed: 'Input supply changed', unsupported: 'Unsupported record format' };
  const blocks = zh ? ['允许充电', '未插电', '采样不可用', '充电配置未就绪', '输入电压异常', '充电故障', '故障恢复中', '电源保护'] :
    ['Charging allowed', 'No input', 'Telemetry unavailable', 'Profile not ready', 'Input out of range', 'Charger fault', 'Fault recovery', 'Power protection'];
  return <>{chargeSessions(page.items, page).map(session => {
    const latestState = [...session.items].reverse().find(r => r.event === 123 && r.data.length === 14);
    const state = latestState?.data[8] ?? 0;
    const ongoing = !(state & 256) ? zh ? '等待插电' : 'Waiting for power' : (state & 255) === 1 ? zh ? '正在充电' : 'Charging' :
      (state & 255) === 2 ? zh ? '充满确认中' : 'Confirming full' : zh ? '充电暂停' : 'Charging paused';
    const stats = [
      [zh ? '累计插电（含多段）' : 'Observed input time (all segments)', hours(session.inputMs)],
      [zh ? '实际充电状态（含多段）' : 'Charging state time (all segments)', hours(session.chargingMs)],
      [zh ? '起点 / 终点 SOC' : 'Start / end SOC', `${soc(session.cycles[0]?.startSoc)} / ${soc(session.cycles.at(-1)?.endSoc)}`],
      [zh ? '充电段数' : 'Charge segments', String(session.cycles.length)],
    ];
    return <Box key={session.id} borderWidth="1px" borderRadius="lg" bg="bg.subtle" p={{ base: 3, md: 4 }} letterSpacing="normal" lineHeight="1.5">
      <Flex justify="space-between" align="center" flexWrap="wrap" gap={2} mb={4}>
        <HStack gap={2} flexWrap="wrap"><Text fontSize="13px" fontWeight="semibold">{zh ? '充电效率测试' : 'Charging performance test'}</Text>
          <Text fontSize="11px" color="fg.muted">#{String(session.id).padStart(3, '0')}</Text>
          <Badge colorPalette={session.incomplete ? 'orange' : 'green'} fontSize="10px">{session.incomplete ? zh ? '部分记录' : 'Partial records' : zh ? '记录完整' : 'Complete records'}</Badge>
        </HStack>
        <Text fontSize="11px" color="fg.muted">{session.end ? reasons[session.end.data[2]] ?? reasons[0] : ongoing}</Text>
      </Flex>
      <SimpleGrid columns={{ base: 2, md: 4 }} gap={3} mb={5}>{stats.map(([label, value]) => <Box key={label} borderLeftWidth="2px" borderColor="border.muted" pl={3}>
        <Text fontSize="10px" color="fg.muted" mb={1}>{label}</Text><Text fontSize={{ base: '13px', md: '15px' }}>{value}</Text>
      </Box>)}</SimpleGrid>
      <Text fontSize="12px" fontWeight="semibold" mb={2}>{zh ? '连续充电段' : 'Continuous charge segments'}</Text>
      {!session.cycles.length && <Text fontSize="11px" color="fg.muted">{zh ? '尚无可确认起点的充电段。' : 'No charge segment with a known start.'}</Text>}
      {session.cycles.map(c => <Box key={c.id} borderWidth="1px" borderRadius="md" p={3} mb={2}>
        <HStack flexWrap="wrap" mb={2} gap={2}><Text fontSize="12px">{zh ? '充电段' : 'Segment'} #{c.id}</Text>
          <Badge colorPalette={c.status === 'complete' ? 'green' : c.status === 'recording' ? 'blue' : 'orange'}>
            {c.status === 'complete' ? zh ? '有效充电至充满' : 'Verified charge to full' : c.status === 'recording' ? zh ? '进行中' : 'In progress' : c.status === 'interrupted' ? zh ? '充电未完成' : 'Interrupted' : zh ? '无法验证' : 'Unverified'}
          </Badge><Text fontSize="11px" color="fg.muted">{c.reason >= 0 ? reasons[c.reason] ?? reasons[0] : ongoing}{c.midCharge ? zh ? ' · 中途开始' : ' · Started mid-charge' : ''}</Text>
        </HStack>
        <SimpleGrid columns={{ base: 1, sm: 2, lg: 3 }} gap={2} fontSize="11px">
          <Text>{zh ? '已观察时长' : 'Observed duration'} · {hours(c.elapsedMs)}</Text>
          <Text>{zh ? '充电状态时长' : 'Charging state time'} · {hours(c.chargingMs)}</Text>
          <Text>SOC · {soc(c.startSoc)} → {soc(c.endSoc)}</Text>
          <Text>{zh ? '电池电压' : 'Battery voltage'} · {voltage(c.startCellMv)} → {voltage(c.endCellMv)}</Text>
          <Text>{zh ? '起始输入电压' : 'Initial input voltage'} · {voltage(c.inputMv)}</Text>
          <Text>{zh ? '平均增长' : 'Average growth'} · {c.ratePerHour?.toFixed(1) ?? '—'} {zh ? '百分点/h' : 'pp/h'}</Text>
          {[500, 800, 900, 1000].map(level => {
            const m = chargeMilestone(c, level);
            return <Text key={level}>{zh ? '起点至' : 'Start to'} {level / 10}% · {m.status === 'already' ? zh ? '起点已超过' : 'Already above at start' : m.status === 'unverified' ? zh ? '无法验证' : 'Unverified' : m.status === 'unreached' ? zh ? '未观察到' : 'Not observed' : hours(m.ms)}</Text>;
          })}
          <Text>{zh ? '起点至确认充满' : 'Start to full'} · {c.status === 'complete' ? hours(c.elapsedMs) : '—'}</Text>
          <Text>20% → 80% · {hours(chargeInterval(c, 200, 800))}</Text>
          <Text>80% → {zh ? '确认充满' : 'full'} · {hours(chargeInterval(c, 800, 'full'))}</Text>
        </SimpleGrid>
        {c.issues.length > 0 && <Text mt={2} fontSize="11px" color="orange.fg">{c.issues.map(i => issues[i]).join('；')}</Text>}
      </Box>)}
      {!session.end && latestState && <Text fontSize="11px" color="fg.muted">{zh ? '最近记录的充电条件' : 'Last recorded charging condition'} · {blocks[latestState.data[9]] ?? (zh ? '未知' : 'Unknown')}</Text>}
      <ChargeTrendChart session={session} language={language} />
      <Box mt={3} pt={3} borderTopWidth="1px" fontSize="11px" color="fg.muted">
        <Text>{zh ? '每次拔电结束当前段。成绩仅覆盖所示起点电量至终点；SOC 100% 与确认充满分别记录。采样未知时段不累计充电状态时长。' : 'Unplugging ends each segment. Results cover only the displayed starting SOC to endpoint. SOC 100% and confirmed full are separate; unknown sample intervals do not accumulate charging state time.'}</Text>
        {session.incomplete && <Text color="orange.fg">{zh ? '记录未结束、不完整或尚未全部落盘；重启后的缺失时段未知。' : 'Records are unfinished, incomplete or pending persistence. Missing time after reboot is unknown.'}</Text>}
      </Box>
    </Box>;
  })}</>;
}
