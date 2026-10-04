'use client';
import { useCallback, useEffect, useRef, useState } from 'react';
import { Badge, Box, Button, Flex, Heading, HStack, Skeleton, Stack, Text } from '@chakra-ui/react';
import { useLanguage } from '@/contexts/language-context';
import { useGamepadConfig } from '@/contexts/gamepad-config-context';
import { FirmwareReleaseCatalog } from './firmware-release-catalog';
import { FirmwareInstallDialog } from './firmware-install-dialog';
import { normalizeTxIapMode, txIapModeLabel } from '@/lib/device-transport/release-install-transfer';
import { accumulateInstallProgress } from '@/lib/device-transport/release-install-progress';
import { isLegacyPhysicalConfirmationRejection, releaseBlockReason, type FirmwareInventory, type PreparedRelease, type ReleaseProgress } from '@/lib/device-transport/release-install-client';
import type { PublicFirmwareRelease } from '@/lib/admin/firmware-types';
import { createInstallMonitor, readInstallTask, readResumableInstallTask, forgetInstallTask, saveInstallTask, type InstallTask } from '@/lib/device-transport/release-install-task';
import type { DeviceImageCatalog } from '@/lib/device-transport/device-feature-types';
import { DeviceTransportError } from '@/lib/device-transport/types';
import { galleryErrorMessage } from '@/lib/gallery-error-message';

const reasons: Record<string, [string, string]> = {
  'catalog-only': ['仅供浏览：需重新打包并验收', 'Catalog only: repackage and validate before installation'],
  'connect-device': ['连接设备后检查兼容性', 'Connect the device to check compatibility'],
  'baseline-required': ['设备需要整机升级协议基线', 'Device needs the whole-device installation baseline'],
  'metadata-mismatch': ['运行槽与固件信息不一致', 'Running slot and metadata disagree'],
  'installation-pending': ['先处理设备上未完成的安装', 'Resolve the pending device installation first'],
  'hardware-mismatch': ['型号或硬件不兼容', 'Model or hardware mismatch'],
  'forbidden-build': ['不是无锁开发产物', 'Not an unlocked development build'],
  'unsupported-protocol': ['不支持此升级协议', 'Unsupported installation protocol'],
  'configuration-incompatible': ['配置格式不兼容，安装会被阻止', 'Configuration format is incompatible'],
  'maintenance-incompatible': ['主控 / TX 维护协议不兼容', 'Controller / TX maintenance protocol mismatch'],
};
const phases: Record<string, [string, string]> = {
  downloading: ['下载发布包', 'Downloading release'], extracting: ['解压与校验', 'Extracting and verifying'],
  'backing-up-tx': ['备份当前 TX', 'Backing up current TX'], 'staging-controller': ['写入主控备用槽', 'Writing inactive controller slot'], 'staging-tx': ['暂存新 TX', 'Staging new TX'],
  'tx-restoring': ['TX 更新失败，正在恢复原版本', 'TX update failed; restoring previous version'], 'rollback-verifying': ['核验恢复后的原版本', 'Verifying restored firmware'],
  restored: ['升级失败，已恢复原版本', 'Update failed; previous version restored'], 'restore-failed': ['TX 恢复失败：需要维护恢复', 'TX recovery failed: maintenance recovery required'],
  timeout: ['TX 升级失败（连接超时）', 'TX update failed (connection timeout)'], backup: ['备份配置', 'Backing up configuration'],
  declaring: ['验证签名安装声明', 'Verifying signed declaration'], receiving: ['暂存组件', 'Staging components'],
  prepared: ['组件已验证，可开始安装', 'Components verified; ready to install'], activating: ['提交安装事务', 'Activating installation'],
  activated: ['设备已接管安装', 'Device owns the installation'], 'tx-writing': ['设备正在更新 TX', 'Device is updating TX'],
  'tx-verified': ['TX 已验证', 'TX verified'], committing: ['切换主控固件', 'Switching controller firmware'],
  verifying: ['核验启动后的整机版本', 'Verifying installed components'], 'waiting-device': ['设备暂时断开，正在更新 TX', 'Device temporarily disconnected; updating TX'],
  completed: ['设备安装事务已完成', 'Device installation transaction completed'], failed: ['安装需要恢复', 'Installation needs recovery'],
  aborted: ['安装已取消', 'Installation cancelled'], idle: ['空闲', 'Idle'], uncertain: ['结果待确认，请重连读取设备状态', 'Result uncertain; reconnect to read device status'],
};
const inventoryReadErrors: Record<string, [string, string]> = {
  timeout: ['设备状态读取超时，请重新连接后重试。', 'Device status read timed out. Reconnect and try again.'],
  disconnected: ['设备已断开，请重新连接。', 'The device disconnected. Reconnect it.'],
  'not-connected': ['设备尚未连接，请先连接设备。', 'The device is not connected. Connect it first.'],
  'bridge-not-ready': ['设备板间通信尚未就绪，请检查连接。', 'The device board link is not ready. Check the connection.'],
  unsupported: ['设备未识别状态查询命令，请检查网页与固件。', 'The device did not recognize the status command. Check the WebConfig and firmware builds.'],
  protocol: ['设备返回的状态数据无效，请重新连接后重试。', 'The device returned invalid status data. Reconnect and try again.'],
};
function inventoryReadError(error: unknown, zh: boolean): string {
  if (error instanceof DeviceTransportError) {
    const copy = inventoryReadErrors[error.code];
    if (copy) return `${copy[zh ? 0 : 1]} (${error.code})`;
  }
  if (error instanceof Error && error.message === 'Device needs the whole-device installation baseline') {
    return zh ? '设备未返回整机安装状态数据；请检查网页与固件构建。' : 'The device did not return whole-device installation status. Check the WebConfig and firmware builds.';
  }
  return zh ? '设备状态读取失败，请检查连接后重试。' : 'Device status read failed. Check the connection and try again.';
}
export function FirmwareContent() {
  const { currentLanguage } = useLanguage(); const zh = currentLanguage === 'zh';
  const { dataIsReady, deviceConnected, firmwareInfo, getReleaseInventory, getDeviceImageCatalog, downloadSelectedRelease,
    installSelectedRelease, releaseInstallAction, reconnectDevice, connectDevice, firmwareUpdating, setFirmwareUpdating, setFinishConfigDisabled } = useGamepadConfig();
  const [inventory, setInventory] = useState<FirmwareInventory | null>(null);
  const [candidate, setCandidate] = useState<PreparedRelease | null>(null);
  const [selectedRelease, setSelectedRelease] = useState<PublicFirmwareRelease | null>(null);
  const [dialogView, setDialogView] = useState<'confirm' | 'progress'>('confirm');
  const [progress, setProgressState] = useState<ReleaseProgress | null>(null);
  const setProgress = useCallback((next: ReleaseProgress | null) => {
    setProgressState(previous => next ? accumulateInstallProgress(previous, next) : null);
  }, []);
  const [error, setError] = useState(''); const [busy, setBusy] = useState(false);
  const [, setLegacyFirmwareGate] = useState(false);
  const [inventoryError, setInventoryError] = useState('');
  const [imageCatalog, setImageCatalog] = useState<DeviceImageCatalog | null>(null);
  const [imageCatalogError, setImageCatalogError] = useState('');
  const [readingInventory, setReadingInventory] = useState(false);
  const [lastManualRead, setLastManualRead] = useState<Date | null>(null);
  const inventoryRequest = useRef<Promise<FirmwareInventory> | null>(null);
  const [expectedDigest, setExpectedDigest] = useState<string | null>(null);
  const [task, setTask] = useState<InstallTask | null>(null);
  const taskRef = useRef<InstallTask | null>(null);
  const monitorRef = useRef<ReturnType<typeof createInstallMonitor> | null>(null);
  const observationRef = useRef({ dataIsReady, deviceConnected, reconnectDevice, getReleaseInventory });
  observationRef.current = { dataIsReady, deviceConnected, reconnectDevice, getReleaseInventory };
  taskRef.current = task;
  useEffect(() => {
    const saved = readResumableInstallTask();
    if (saved) { if (['waiting', 'timeout'].includes(saved.result)) setFirmwareUpdating(true); setTask(saved); setSelectedRelease(saved.release); setDialogView('progress'); setExpectedDigest(saved.digest);
      setProgress(saved.progress || { stage: saved.result === 'waiting' ? 'waiting-device' : saved.result, overallPercent: 80, stepIndex: 6 }); }
  }, [setFirmwareUpdating, setProgress]);
  useEffect(() => {
    if (!task) return;
    const monitor = createInstallMonitor({ task,
      connected: () => observationRef.current.deviceConnected,
      reconnect: () => observationRef.current.reconnectDevice(),
      waitReady: async () => {
        // Opening a HID handle completes before configuration initialization.
        // Wait in memory; do not open another connection or issue status requests.
        const until = Date.now() + 30_000;
        while (!observationRef.current.dataIsReady) {
          if (!observationRef.current.deviceConnected || Date.now() >= until)
            throw new Error('Connection initialization incomplete');
          await new Promise(resolve => setTimeout(resolve, 100));
        }
      },
      query: () => observationRef.current.getReleaseInventory(),
      changed: (next, status) => {
        setTask(next);
        if (next.result === 'completed') {
          try { forgetInstallTask(next.sessionId); } catch { /* A later mount ignores completed records. */ }
        } else {
          try { saveInstallTask(next); } catch { setError(zh ? '任务记录保存失败，请保持此页面打开。' : 'Task persistence failed; keep this page open.'); }
        }
        if (status) { setInventory(status); setInventoryError(''); }
        setProgress(next.progress || { stage: next.result === 'waiting' ? (status?.phase || 'waiting-device') : next.result });
      },
    });
    monitorRef.current = monitor; monitor.start();
    return () => { monitor.stop(); if (monitorRef.current === monitor) monitorRef.current = null; };
    // The monitor owns its current task. Render changes must not restart requests or deadlines.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [task?.sessionId]);
  const manualReconnect = () => {
    // A live connection only needs a status read. Resetting that connection
    // interrupts initialization and may never emit a new connected event.
    void monitorRef.current?.manual(
      observationRef.current.deviceConnected ? undefined : connectDevice,
    ).catch(e => setError(String(e)));
  };
  const selectionPending = useRef(false);
  const installPending = useRef(false);
  const refresh = useCallback(async (manual = false) => {
    if (taskRef.current && monitorRef.current) { await monitorRef.current.manual(); return; }
    if (!dataIsReady || !deviceConnected) {
      if (manual) setInventoryError(zh
        ? '设备尚未完成连接和配置读取，请先检查连接状态。'
        : 'The device connection and configuration read are not ready. Check the connection first.');
      return;
    }
    if (manual) { setReadingInventory(true); setInventoryError(''); setImageCatalogError(''); setImageCatalog(null); }
    const request = inventoryRequest.current ?? getReleaseInventory();
    inventoryRequest.current = request;
    try { const next = await request; setInventory(next); setInventoryError('');
      if (!busy && !selectionPending.current && !installPending.current && !taskRef.current)
        setProgress(['idle', 'aborted', 'completed'].includes(next.phase) ? null : { stage: next.phase });
    } catch (e) { setInventoryError(inventoryReadError(e, zh)); }
    finally { if (inventoryRequest.current === request) inventoryRequest.current = null; }
    if (manual) {
      try { setImageCatalog(await getDeviceImageCatalog()); }
      catch (e) { setImageCatalogError(galleryErrorMessage(e, zh ? 'zh' : 'en')); }
      finally { setLastManualRead(new Date()); setReadingInventory(false); }
    }
  }, [dataIsReady, deviceConnected, getReleaseInventory, getDeviceImageCatalog, busy, zh, setProgress]);
  useEffect(() => { if (!task) void refresh(); }, [refresh, task]);
  useEffect(() => {
    setFinishConfigDisabled(busy || firmwareUpdating || task?.result === 'waiting');
    return () => setFinishConfigDisabled(false);
  }, [busy, firmwareUpdating, task?.result, setFinishConfigDisabled]);
  const select = (release: PublicFirmwareRelease) => {
    if (!inventory || releaseBlockReason(release, inventory) || selectionPending.current || installPending.current || busy) return;
    setTask(null); setSelectedRelease(release); setDialogView('confirm'); setCandidate(null); setError(''); setProgress(null); setLegacyFirmwareGate(false);
  };
  const install = async (prepared: PreparedRelease) => {
    if (installPending.current) return;
    installPending.current = true;
    setBusy(true); setError(''); setLegacyFirmwareGate(false); setExpectedDigest(prepared.digest);
    let activatedSession: string | undefined;
    try { await installSelectedRelease(prepared, next => {
      setProgress(next);
      if (next.stage === 'activating' || next.stage === 'waiting-device') { activatedSession = next.sessionId; const saved = readInstallTask(); if (saved) setTask(saved); }
    }); setCandidate(null); }
    catch (e) {
      if (isLegacyPhysicalConfirmationRejection(e)) {
        setError(zh
          ? '设备升级协议基线不支持 TX 读回备份，请先通过既有维护入口更新主控和 TX。'
          : 'This baseline does not support TX readback backups. Update the controller and TX through the existing maintenance path first.');
        setLegacyFirmwareGate(true);
        setProgress(null); setExpectedDigest(null);
      } else {
        setError(e instanceof Error ? e.message : String(e)); setProgress({ stage: 'failed' });
        const saved = readInstallTask(); setTask(saved?.sessionId === activatedSession ? saved : null); setCandidate(null);
      }
    }
    finally { installPending.current = false; setBusy(false); void refresh(); }
  };
  const startInstallation = async () => {
    if (!selectedRelease || !inventory || selectionPending.current || installPending.current) return;
    setDialogView('progress'); setError('');
    if (candidate) { void install(candidate); return; }
    selectionPending.current = true;
    setBusy(true); setProgress({ stage: 'downloading' });
    try {
      const prepared = await downloadSelectedRelease(selectedRelease.id, inventory, setProgress);
      setCandidate(prepared);
      selectionPending.current = false;
      await install(prepared);
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
      setProgress({ stage: 'failed' }); setBusy(false);
    } finally { selectionPending.current = false; }
  };
  const recover = async (action: 'abort' | 'retry' | 'activate') => {
    if (!inventory) return; setBusy(true); setError('');
    try { await releaseInstallAction(action, inventory.sessionId); await refresh(); }
    catch (e) { setError(e instanceof Error ? e.message : String(e)); }
    finally { setBusy(false); }
  };
  const translate = (values: Record<string, [string, string]>, key: string) => values[key]?.[zh ? 0 : 1] || key;
  const verified = inventory?.installationState === 'installed' && inventory.phase === 'completed' &&
    (!expectedDigest || expectedDigest === inventory.confirmedDigest);
  const stm32Version = inventory?.stm32.version && inventory.stm32.version !== '0.0.0'
    ? inventory.stm32.version : firmwareInfo?.firmware.version || inventory?.stm32.version || '—';
  const currentSlot = inventory?.currentSlot || firmwareInfo?.firmware.currentSlot || '—';
  // Metadata arrives before whole-device inventory. Reveal the card only when
  // both reads have settled; a missing TX identity is a result, not a pending read.
  const inventoryLoading = readingInventory || (deviceConnected &&
    (!dataIsReady || (!inventory && !inventoryError)));
  return <Stack pt="24px" px={{ base: '16px', md: '24px' }} pb="32px" gap="5" w="full" maxW="1120px" mx="auto" minW={0} fontSize="14px">
    <Flex justify="space-between" align={{ base: 'start', md: 'center' }} direction={{ base: 'column', md: 'row' }} gap="3">
      <Box><Heading as="h1" fontSize="24px">{zh ? '固件更新' : 'Firmware updates'}</Heading>
        <Text fontSize="14px" color="fg.muted" mt="1">{zh ? '查看当前固件，选择适合设备的版本。' : 'Review the installed firmware and choose a compatible release.'}</Text></Box>
      <Button h="36px" fontSize="14px" variant="surface" disabled={busy} loading={readingInventory}
        onClick={() => void refresh(true)}>{zh ? '刷新设备状态' : 'Refresh device status'}</Button>
    </Flex>
    <Box data-testid="installed-firmware-card" aria-busy={inventoryLoading}
      borderWidth="1px" borderRadius="xl" px={{ base: '4', md: '5' }} py="4" minH="158px">
      {inventoryLoading ? <Stack gap="3" role="status" aria-label={zh ? '正在读取当前固件' : 'Loading installed firmware'}>
        <HStack justify="space-between" align="start" gap="2" aria-hidden="true">
          <Stack gap="1" flex="1" minW={0}>
            <Skeleton h="24px" w="180px" maxW="full" />
            <Skeleton h="1lh" w="140px" maxW="full" />
          </Stack>
          <Skeleton h="20px" w="60px" />
        </HStack>
        <HStack gap="5" aria-hidden="true">
          <Skeleton h="1lh" w="110px" /><Skeleton h="1lh" w="90px" />
        </HStack>
        <Box pt="2" borderTopWidth="1px" aria-hidden="true"><Skeleton h="1lh" w="100px" /></Box>
      </Stack> : inventory ? <Stack gap="3">
      <HStack justify="space-between" align="start" wrap="wrap" gap="2">
        <Stack gap="1"><Heading as="h2" fontSize="18px" lineHeight="24px">{zh ? '当前固件' : 'Installed firmware'}</Heading>
          <Text fontSize="16px" fontWeight="semibold">{verified ? `XORA ${inventory.confirmedVersion}` : (zh ? '整机版本未确认' : 'Whole-device release unconfirmed')}</Text></Stack>
        <Badge colorPalette={verified ? 'green' : 'orange'}>{verified ? (zh ? '已核验' : 'Verified') : (inventory?.installationState === 'mixed' ? (zh ? '混合版本' : 'Mixed components') : (zh ? '未确认' : 'Unconfirmed'))}</Badge>
      </HStack>
      <HStack gap="5" wrap="wrap"><Text>{zh ? '主控' : 'Controller'} <Text as="span" fontWeight="semibold">{stm32Version}</Text></Text>
        <Text>TX <Text as="span" fontWeight="semibold">{inventory?.tx?.version || '—'}</Text></Text></HStack>
      {!verified && <Text fontSize="12px" color="fg.muted">{zh ? '当前组件尚未核验为完整整机版本。' : 'The installed components have not been verified as a complete release.'}</Text>}
      {inventory && !inventory.tx && <Text fontSize="12px" color="orange.500">{zh ? '未读到 TX 固件身份，请检查板间通信。' : 'TX firmware identity was unavailable. Check the board link.'}</Text>}
      <Box as="details" fontSize="12px" color="fg.muted" pt="2" borderTopWidth="1px">
        <Box as="summary" cursor="pointer" fontWeight="semibold">{zh ? '设备详情' : 'Device details'}</Box>
        <Stack mt="3" gap="2" overflowWrap="anywhere">
          <Text>{zh ? '运行槽' : 'Running slot'}: {currentSlot}</Text>
          {firmwareInfo?.firmware.buildDate && <Text>{zh ? '主控元数据构建时间' : 'Controller metadata build date'}: {firmwareInfo.firmware.buildDate}</Text>}
          {inventory && <>
            <Text>{zh ? '主控构建' : 'Controller build'}: {inventory.stm32.buildId || '—'}</Text>
            <Text>{zh ? 'TX 构建' : 'TX build'}: {inventory.tx?.buildId || '—'}</Text>
          </>}
          {inventory?.sessionId && <Text>{zh ? '上次 TX 安装：' : 'Last TX installation: '}{txIapModeLabel(inventory.txInstallMode, zh)}</Text>}
          {inventory && (normalizeTxIapMode(inventory.txRecoveryMode) !== 'unknown' || inventory.recoveryResult === 'restored' || inventory.recoveryResult === 'failed') && <Text>{zh ? '上次 TX 恢复：' : 'Last TX recovery: '}{txIapModeLabel(inventory.txRecoveryMode, zh)}</Text>}
          {(inventory?.stm32.buildId === 'unidentified' || inventory?.tx?.buildId === 'unidentified') && <Text>{zh ? '开发构建的 0.0.0 / unidentified 只是发布身份占位值；STM32 显示版本优先采用固件元数据。' : 'Development builds use 0.0.0 / unidentified as release identity placeholders. The STM32 version shown above prefers firmware metadata.'}</Text>}
          {inventory?.confirmedVersion && !verified && <Text>{zh ? '上次确认版本' : 'Last confirmed release'}: {inventory.confirmedVersion}</Text>}
          {imageCatalog && <Text>{zh ? '图片能力' : 'Image capability'}: {zh ? `最多 ${imageCatalog.maxUserFrames} 帧，传输版本 ${imageCatalog.imageTransferVersion}` : `up to ${imageCatalog.maxUserFrames} frames, transfer version ${imageCatalog.imageTransferVersion}`}</Text>}
          {imageCatalogError && <Text color="red.500">{zh ? '图片能力读取失败' : 'Image capability read failed'}: {imageCatalogError}</Text>}
          {lastManualRead && <Text aria-live="polite">{zh ? '上次检测' : 'Last check'}: {lastManualRead.toLocaleTimeString(zh ? 'zh-CN' : 'en-US')}</Text>}
        </Stack>
      </Box>
    </Stack> : <Stack gap="3">
      <Heading as="h2" fontSize="18px" lineHeight="24px">{zh ? '当前固件' : 'Installed firmware'}</Heading>
      <Text color="fg.muted">{inventoryError
        ? (zh ? '当前固件信息读取失败，请刷新设备状态重试。' : 'Installed firmware could not be read. Refresh device status to try again.')
        : (zh ? '连接设备后查看当前固件。' : 'Connect the device to view installed firmware.')}</Text>
    </Stack>}</Box>
    {inventoryError && <Text role="alert" color="red.500" overflowWrap="anywhere">{zh ? '设备状态读取失败：' : 'Device status read failed: '}{inventoryError}</Text>}
    {error && !selectedRelease && <Text role="alert" color="red.500" overflowWrap="anywhere">{error}</Text>}
    {progress && !selectedRelease && <Box aria-live="polite" borderWidth="1px" borderRadius="xl" p="5"><Stack gap="3">
      <Heading as="h2" fontSize="18px">{translate(phases, progress.stage)}</Heading>
      {progress.component && <Text>{progress.component}: {progress.received} / {progress.total} bytes</Text>}
      {inventory?.targetVersion && <Text>{zh ? '目标版本：' : 'Target: '}{inventory.targetVersion}</Text>}
      {inventory?.error && <Text color="red.500">{inventory.error}</Text>}

      <HStack wrap="wrap">
        {inventory?.canAbort && <Button h="36px" fontSize="14px" disabled={busy} onClick={() => void recover('abort')}>{zh ? '取消未激活安装' : 'Cancel staged installation'}</Button>}
        {task && <Button h="36px" fontSize="14px" onClick={manualReconnect}>{zh ? '重新连接设备' : 'Reconnect device'}</Button>}
      </HStack>
    </Stack></Box>}
    <FirmwareReleaseCatalog getReleaseAction={release => {
      const reason = releaseBlockReason(release, inventory);
      const same = verified && inventory.confirmedVersion === release.manifest.version;
      const downgrade = verified && release.manifest.version.localeCompare(inventory.confirmedVersion, undefined, { numeric: true }) < 0;
      return { label: same ? (zh ? '重新安装' : 'Reinstall') : downgrade ? (zh ? '降级安装' : 'Downgrade') : (zh ? '安装此版本' : 'Install release'),
        disabled: busy || task?.result === 'waiting' || Boolean(reason), reason: reason ? translate(reasons, reason) : undefined,
        onClick: () => select(release) };
    }} />
    <FirmwareInstallDialog release={selectedRelease} zh={zh} view={dialogView} progress={progress} deviceConnected={deviceConnected}
      activatedAt={task?.activatedAt || progress?.activatedAt}
      txRecoveryMode={task?.txRecoveryMode}
      error={error || (task?.result === 'restore-failed' ? `${task.error || ''} (${inventory?.errorCode || 'TX_RESTORE_FAILED'})` : '')} busy={busy || task?.result === 'waiting'} retryable={false} onReconnect={manualReconnect} canReconnect={Boolean(task)}
      progressLabel={translate(phases, progress?.stage || 'downloading')}
      onClose={() => { if (!busy && task?.result !== 'waiting') {
        setSelectedRelease(null); setCandidate(null); setError('');
        if (!task || task.result === 'completed') { setTask(null); setProgress(null); setExpectedDigest(null); }
        setLegacyFirmwareGate(false);
      } }}
      onConfirm={() => void startInstallation()} onRetry={() => void startInstallation()}
      />
  </Stack>;
}
