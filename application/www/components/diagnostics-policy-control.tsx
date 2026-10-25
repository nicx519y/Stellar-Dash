"use client";
import { useEffect, useRef, useState } from 'react';
import { Box, Button, Grid, Text } from '@chakra-ui/react';
import { useLogOperation } from '@/lib/log-operation';
import { readReceiverDiagnosticsPolicy } from '@/lib/device-transport/rx-log-client';
import { Switch } from '@/components/ui/switch';
import { useGamepadFields } from '@/contexts/gamepad-config-context';
import { useLanguage } from '@/contexts/language-context';
import { type DiagnosticsPolicy, type DiagnosticsSettings } from '@/lib/diagnostics-policy';

export function DiagnosticsPolicyControl({ connected, disabled }: { connected: boolean; disabled: boolean }) {
  const { diagnosticsPolicy, flushDeferredConfig } = useGamepadFields('diagnosticsPolicy', 'flushDeferredConfig');
  const { currentLanguage } = useLanguage();
  const zh = currentLanguage === 'zh';
  const [policy, setPolicy] = useState<DiagnosticsPolicy | null>(null);
  const [busy, setBusy] = useState(false);
  const [rx, setRx] = useState<DiagnosticsSettings | null>(null);
  const ownsOperation = useRef(false);
  const [error, setError] = useState(false);
  const epoch = useRef(0);
  const action = useRef(diagnosticsPolicy);
  action.current = diagnosticsPolicy;
  useEffect(() => {
    const token = ++epoch.current;
    setPolicy(null); setRx(null); setError(false); setBusy(false);
    if (connected) {
      setBusy(true);
      void action.current().then(p => { if (epoch.current === token) setPolicy(p); })
        .catch(() => { if (epoch.current === token) setError(true); })
        .finally(() => { if (epoch.current === token) setBusy(false); });
    }
    return () => { ++epoch.current; if (ownsOperation.current) { ownsOperation.current = false; useLogOperation.getState().setOperation(null); } };
  }, [connected]);
  async function update(enabled?: DiagnosticsSettings) {
    if (busy || disabled || !connected) return;
    const token = ++epoch.current;
    setBusy(true); setError(false); setRx(null);
    ownsOperation.current = true; useLogOperation.getState().setOperation("policy");
    try {
      if (enabled !== undefined) await flushDeferredConfig();
      const p = await action.current(enabled);
      if (epoch.current === token) setPolicy(p);
    } catch {
      // A timeout is not an acknowledgement. Re-read before another write.
      if (epoch.current === token) { setPolicy(null); setError(true); }
    } finally { if (epoch.current === token) { setBusy(false); ownsOperation.current = false; useLogOperation.getState().setOperation(null); } }
  }
  async function checkReceiver() {
    if (busy || disabled || !policy) return;
    const token = ++epoch.current;
    setBusy(true); setError(false); setRx(null);
    ownsOperation.current = true; useLogOperation.getState().setOperation('policy');
    try {
      const enabled = process.env.NEXT_PUBLIC_DEVICE_TRANSPORT === 'mock' ? policy : await readReceiverDiagnosticsPolicy();
      if (token === epoch.current) setRx(enabled);
    } catch { if (token === epoch.current) setError(true); }
    finally { if (token === epoch.current) { setBusy(false); ownsOperation.current = false; useLogOperation.getState().setOperation(null); } }
  }
  const state = (s: DiagnosticsPolicy['tx']) => s === 'applied' ? (zh ? '已生效' : 'Applied') :
    s === 'unsupported' ? (zh ? '需更新固件' : 'Update firmware') : (zh ? '待确认' : 'Unconfirmed');
  const unknownText = zh ? '尚未读取设备状态' : 'Device state has not been read';
  const errorText = zh ? '状态未确认，请检查连接或更新固件后重试。' : 'Unconfirmed. Check the connection or update firmware, then retry.';
  const syncingText = zh ? '正在同步开关状态…' : 'Syncing switch settings…';
  const policyText = policy ? `${zh ? '主控' : 'Main controller'}: ${state(policy.controller)} · TX: ${state(policy.tx)} · RX: ${state(rx !== null && rx.loggingEnabled === policy.loggingEnabled && rx.monitoringEnabled === policy.monitoringEnabled ? 'applied' : policy.rx)}` : unknownText;
  // All response states share a grid cell. Invisible sizing text reserves the
  // wrapped height at every viewport/font size, without clipping real errors.
  const statusSizes = [unknownText, errorText, syncingText,
    `${zh ? '主控' : 'Main controller'}: ${state('applied')} · TX: ${state('unsupported')} · RX: ${state('unsupported')}`];
  return <Box data-testid="diagnostics-policy" borderWidth="1px" borderRadius="md" p={3} flexShrink={0}>
    <Grid alignItems="center" templateColumns={{ base: 'minmax(0, 1fr)', sm: 'repeat(2, minmax(0, 1fr))', xl: 'minmax(0, 1fr) minmax(0, 1fr) 112px' }} gap={3}>
      {(['loggingEnabled', 'monitoringEnabled'] as const).map(field => <Switch
        key={`${field}-${policy ? 'known' : 'unknown'}`} colorPalette="green" checked={policy?.[field] ?? false}
        disabled={!connected || !policy || disabled || busy}
        onCheckedChange={e => { if (policy) void update({ loggingEnabled: policy.loggingEnabled, monitoringEnabled: policy.monitoringEnabled, [field]: e.checked }); }}>
        {field === 'loggingEnabled' ? (zh ? '设备日志记录' : 'Device logging') : (zh ? '诊断监控采集' : 'Diagnostic monitoring')}
      </Switch>)}
      <Button size="xs" variant="ghost" w="112px" justifySelf="start" disabled={!connected || busy || disabled} onClick={() => void update()}>
        {zh ? '检查状态' : 'Check status'}
      </Button>
    </Grid>
    <Text mt={2} fontSize="xs" color="fg.muted">
      {zh ? '两个开关独立生效；均关闭时停止日志与诊断采集。历史日志保留，电池测试独立控制。' :
        'Independent controls. Turn both off to stop logging and diagnostic capture. History is retained; battery tests have separate controls.'}
    </Text>
    <Grid mt={1} fontSize="xs" role="status" aria-live="polite" color={error ? 'orange.fg' : 'fg.muted'}>
      {statusSizes.map(text => <Text key={text} gridArea="1 / 1" visibility="hidden" aria-hidden="true">{text}</Text>)}
      <Text gridArea="1 / 1">{error ? errorText : busy ? syncingText : policyText}</Text>
    </Grid>
    <Button alignSelf="start" mt={1} size="xs" variant="ghost" disabled={!connected || !policy || disabled || busy} onClick={() => void checkReceiver()}>
      {zh ? '选择 RX 并检查状态' : 'Select RX and check status'}
    </Button>
    <Text mt={1} fontSize="xs" color="fg.muted">
      {zh ? 'RX 在下次 RF 连接时同步；未确认前不代表全链路已关闭。' :
        'RX synchronizes on the next RF connection. Full-link shutdown is unconfirmed until RX acknowledges.'}
    </Text>
  </Box>;
}
