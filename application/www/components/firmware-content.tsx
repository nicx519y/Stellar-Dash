'use client';
import { useCallback, useEffect, useRef, useState } from 'react';
import { Badge, Box, Button, Heading, HStack, Stack, Text } from '@chakra-ui/react';
import { useLanguage } from '@/contexts/language-context';
import { useGamepadConfig } from '@/contexts/gamepad-config-context';
import { FirmwareReleaseCatalog } from './firmware-release-catalog';
import { FirmwareReleaseDetails } from './firmware-release-details';
import { releaseBlockReason, type FirmwareInventory, type PreparedRelease, type ReleaseProgress } from '@/lib/device-transport/release-install-client';
import type { PublicFirmwareRelease } from '@/lib/admin/firmware-types';
import { scheduleAuthorizedReconnect } from '@/lib/device-transport/authorized-reconnect';
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
  'maintenance-incompatible': ['STM32 / TX 维护协议不兼容', 'STM32 / TX maintenance protocol mismatch'],
};
const phases: Record<string, [string, string]> = {
  'awaiting-confirmation': ['备份已保存，请在设备上授权后继续', 'Backup saved; authorize on the device to continue'],
  downloading: ['下载并验证发布包', 'Downloading and verifying release'], backup: ['备份配置', 'Backing up configuration'],
  declaring: ['验证签名安装声明', 'Verifying signed declaration'], receiving: ['暂存组件', 'Staging components'],
  prepared: ['组件已验证，可开始安装', 'Components verified; ready to install'], activating: ['提交安装事务', 'Activating installation'],
  activated: ['设备已接管安装', 'Device owns the installation'], 'tx-writing': ['设备正在更新 TX', 'Device is updating TX'],
  'tx-verified': ['TX 已验证', 'TX verified'], committing: ['提交 STM32 固件', 'Committing STM32 firmware'],
  verifying: ['核验启动后的整机版本', 'Verifying installed components'], 'waiting-device': ['等待设备返回', 'Waiting for device'],
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
    installSelectedRelease, releaseInstallAction, reconnectDevice, connectDevice, firmwareUpdating, setFinishConfigDisabled } = useGamepadConfig();
  const [inventory, setInventory] = useState<FirmwareInventory | null>(null);
  const [candidate, setCandidate] = useState<PreparedRelease | null>(null);
  const [progress, setProgress] = useState<ReleaseProgress | null>(null);
  const [error, setError] = useState(''); const [busy, setBusy] = useState(false);
  const [inventoryError, setInventoryError] = useState('');
  const [imageCatalog, setImageCatalog] = useState<DeviceImageCatalog | null>(null);
  const [imageCatalogError, setImageCatalogError] = useState('');
  const [readingInventory, setReadingInventory] = useState(false);
  const [lastManualRead, setLastManualRead] = useState<Date | null>(null);
  const inventoryRequest = useRef<Promise<FirmwareInventory> | null>(null);
  const [expectedDigest, setExpectedDigest] = useState<string | null>(null);
  const reconnectStarted = useRef(0);
  const confirmation = useRef<{ resolve: () => void; reject: (error: Error) => void } | null>(null);
  const [awaitingConfirmation, setAwaitingConfirmation] = useState(false);
  useEffect(() => () => confirmation.current?.reject(new Error('Installation confirmation cancelled')), []);
  const refresh = useCallback(async (manual = false) => {
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
      if (!busy && !['idle', 'aborted'].includes(next.phase)) setProgress({ stage: next.phase });
    } catch (e) { setInventoryError(inventoryReadError(e, zh)); }
    finally { if (inventoryRequest.current === request) inventoryRequest.current = null; }
    if (manual) {
      try { setImageCatalog(await getDeviceImageCatalog()); }
      catch (e) { setImageCatalogError(galleryErrorMessage(e, zh ? 'zh' : 'en')); }
      finally { setLastManualRead(new Date()); setReadingInventory(false); }
    }
  }, [dataIsReady, deviceConnected, getReleaseInventory, getDeviceImageCatalog, busy, zh]);
  useEffect(() => {
    void refresh();
    if (!firmwareUpdating) return;
    const timer = setInterval(() => void refresh(), 5000);
    return () => clearInterval(timer);
  }, [refresh, firmwareUpdating]);
  useEffect(() => {
    setFinishConfigDisabled(busy || firmwareUpdating);
    return () => setFinishConfigDisabled(false);
  }, [busy, firmwareUpdating, setFinishConfigDisabled]);
  useEffect(() => {
    if (deviceConnected) { reconnectStarted.current = 0; return; }
    if (!firmwareUpdating) { setInventory(null); return; }
    if (!reconnectStarted.current) reconnectStarted.current = Date.now();
    setProgress({ stage: 'waiting-device' });
    const stop = scheduleAuthorizedReconnect(reconnectDevice, () => setProgress({ stage: 'uncertain' }));
    const timer = setTimeout(() => { stop(); setProgress({ stage: 'uncertain' }); }, 90000);
    return () => { stop(); clearTimeout(timer); };
  }, [deviceConnected, firmwareUpdating, reconnectDevice]);
  const select = async (release: PublicFirmwareRelease) => {
    if (!inventory) return; setBusy(true); setError(''); setCandidate(null); setProgress({ stage: 'downloading' });
    try { setCandidate(await downloadSelectedRelease(release.id, inventory)); setProgress(null); }
    catch (e) { setError(e instanceof Error ? e.message : String(e)); setProgress(null); }
    finally { setBusy(false); }
  };
  const install = async () => {
    if (!candidate) return; setBusy(true); setError(''); setExpectedDigest(candidate.digest);
    try { await installSelectedRelease(candidate, setProgress, () => new Promise<void>((resolve, reject) => {
      confirmation.current = { resolve, reject }; setAwaitingConfirmation(true);
    })); setCandidate(null); }
    catch (e) { setError(e instanceof Error ? e.message : String(e)); setProgress({ stage: 'uncertain' }); }
    finally { confirmation.current = null; setAwaitingConfirmation(false); setBusy(false); void refresh(); }
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
  return <Stack p="18px" gap="6" w="full" maxW="1100px" mx="auto">
    <Box borderWidth="1px" borderRadius="xl" p="5"><Stack gap="3">
      <HStack justify="space-between"><Heading size="lg">{zh ? '本机固件' : 'Installed firmware'}</Heading>
        <Badge colorPalette={verified ? 'green' : 'orange'}>{verified ? (zh ? '已核验' : 'Verified') : (inventory?.installationState === 'mixed' ? (zh ? '混合版本' : 'Mixed components') : (zh ? '未确认整机版本' : 'Release unconfirmed'))}</Badge></HStack>
      <Text>{zh ? '整机包：' : 'Release: '}{verified ? inventory.confirmedVersion : (zh ? '未知或未完成' : 'Unknown or incomplete')}</Text>
      <Text>STM32: {stm32Version} · TX: {inventory?.tx?.version || '—'} · {zh ? '运行槽' : 'Running slot'}: {currentSlot}</Text>
      {firmwareInfo?.firmware.buildDate && <Text fontSize="sm" color="fg.muted">{zh ? 'STM32 元数据构建时间：' : 'STM32 metadata build date: '}{firmwareInfo.firmware.buildDate}</Text>}
      {inventory && <Text fontSize="sm" color="fg.muted">STM32 build: {inventory.stm32.buildId || '—'} · TX build: {inventory.tx?.buildId || '—'}</Text>}
      {inventory && !inventory.tx && <Text fontSize="sm" color="orange.500">{zh ? '未读到 TX 固件身份，请检查 STM32 与 TX 的板间通信。' : 'TX firmware identity was unavailable. Check the STM32–TX board link.'}</Text>}
      {(inventory?.stm32.buildId === 'unidentified' || inventory?.tx?.buildId === 'unidentified') && <Text fontSize="sm" color="fg.muted">{zh ? '开发构建的 0.0.0 / unidentified 是发布身份占位值，不能据此判断固件新旧；STM32 显示版本优先采用固件元数据。' : 'Development builds use 0.0.0 / unidentified as release identity placeholders; these do not indicate firmware age. The displayed STM32 version prefers firmware metadata.'}</Text>}
      {imageCatalog && <Text fontSize="sm">{zh ? '图片能力：' : 'Image capability: '}{zh ? `最多 ${imageCatalog.maxUserFrames} 帧，传输版本 ${imageCatalog.imageTransferVersion}` : `up to ${imageCatalog.maxUserFrames} frames, transfer version ${imageCatalog.imageTransferVersion}`}</Text>}
      {imageCatalogError && <Text fontSize="sm" color="red.500">{zh ? '图片能力读取失败：' : 'Image capability read failed: '}{imageCatalogError}</Text>}
      {inventory?.confirmedVersion && !verified && <Text>{zh ? '上次确认版本：' : 'Last confirmed release: '}{inventory.confirmedVersion}</Text>}
      <Button alignSelf="start" variant="surface" disabled={busy} loading={readingInventory} onClick={() => void refresh(true)}>{zh ? '读取设备状态' : 'Read device status'}</Button>
      {lastManualRead && <Text fontSize="sm" color="fg.muted" aria-live="polite">{zh ? '上次检测：' : 'Last check: '}{lastManualRead.toLocaleTimeString(zh ? 'zh-CN' : 'en-US')}</Text>}
      {!deviceConnected && <Button alignSelf="start" onClick={() => void connectDevice().catch(e => setError(String(e)))}>{zh ? '连接设备' : 'Connect device'}</Button>}
      {deviceConnected && !dataIsReady && <Text>{zh ? '正在读取设备配置…' : 'Reading device configuration…'}</Text>}
    </Stack></Box>
    {inventoryError && <Text role="alert" color="red.500" overflowWrap="anywhere">{zh ? '设备状态读取失败：' : 'Device status read failed: '}{inventoryError}</Text>}
    {error && <Text role="alert" color="red.500" overflowWrap="anywhere">{error}</Text>}
    {progress && <Box aria-live="polite" borderWidth="1px" borderRadius="xl" p="5"><Stack gap="3">
      <Heading size="md">{translate(phases, progress.stage)}</Heading>
      {progress.component && <Text>{progress.component}: {progress.received} / {progress.total} bytes</Text>}
      {inventory?.targetVersion && <Text>{zh ? '目标版本：' : 'Target: '}{inventory.targetVersion}</Text>}
      {inventory?.error && <Text color="red.500">{inventory.error}</Text>}
      {inventory?.canRetry && <Text>{zh ? 'TX 无法连接时，在设备上释放后按住 GPIO1 + FN 两秒重试。' : 'If TX cannot connect, release then hold GPIO1 + FN on the device for two seconds to retry.'}</Text>}
      <HStack wrap="wrap">
        {inventory?.canAbort && <Button disabled={busy} onClick={() => void recover('abort')}>{zh ? '取消未激活安装' : 'Cancel staged installation'}</Button>}
        {inventory?.phase === 'prepared' && <Button disabled={busy} onClick={() => void recover('activate')}>{zh ? '继续安装' : 'Continue installation'}</Button>}
        {inventory?.canRetry && <Button disabled={busy} onClick={() => void recover('retry')}>{zh ? '重试目标版本' : 'Retry target release'}</Button>}
        {!deviceConnected && <Button onClick={() => void reconnectDevice()}>{zh ? '重新连接' : 'Reconnect'}</Button>}
      </HStack>
      {(inventory?.canAbort || inventory?.canRetry || inventory?.phase === 'prepared') && <Text fontSize="sm">{zh ? '操作前，在设备上释放后按住 GPIO1 + FN 两秒授权。' : 'Before the action, release then hold GPIO1 + FN for two seconds to authorize it.'}</Text>}
    </Stack></Box>}
    {candidate && <Box borderWidth="2px" borderColor="blue.500" borderRadius="xl" p="5"><Stack gap="3">
      <Heading size="md">{zh ? '确认安装 XORA ' : 'Install XORA '}{candidate.release.manifest.version}</Heading>
      <Text>{zh ? '保留配置、校准和配对。仅安装 STM32 + TX；RX 单独更新。TX 更新期间会断连，请保持供电。' : 'Preserves configuration, calibration and pairing. Installs STM32 + TX; RX updates are separate. Keep power connected while TX disconnects.'}</Text>
      <FirmwareReleaseDetails manifest={candidate.release.manifest} zh={zh} />
      <Text>{awaitingConfirmation
        ? (zh ? '备份已保存。在设备上释放后按住 GPIO1 + FN 两秒，然后点击授权完成。' : 'Backup saved. Release then hold GPIO1 + FN for two seconds, then continue.')
        : (zh ? '先保存并备份配置，完成后再进行设备授权。' : 'Save and back up configuration first, then authorize on the device.')}</Text>
      <HStack>{awaitingConfirmation
        ? <Button onClick={() => { setAwaitingConfirmation(false); confirmation.current?.resolve(); }}>{zh ? '授权完成，继续安装' : 'Authorized, continue installation'}</Button>
        : <Button loading={busy} onClick={() => void install()}>{zh ? '准备安装' : 'Prepare installation'}</Button>}
        <Button disabled={busy && !awaitingConfirmation} variant="surface" onClick={() => {
          confirmation.current?.reject(new Error(zh ? '安装已取消' : 'Installation cancelled')); setCandidate(null);
        }}>{zh ? '返回列表' : 'Back to list'}</Button></HStack>
    </Stack></Box>}
    <FirmwareReleaseCatalog renderAction={release => {
      const reason = releaseBlockReason(release, inventory);
      const same = verified && inventory.confirmedVersion === release.manifest.version;
      const downgrade = verified && release.manifest.version.localeCompare(inventory.confirmedVersion, undefined, { numeric: true }) < 0;
      return <Stack align="start"><Button disabled={busy || Boolean(reason)} onClick={() => void select(release)}>{same ? (zh ? '重新安装' : 'Reinstall') : downgrade ? (zh ? '降级安装' : 'Downgrade') : (zh ? '安装此版本' : 'Install release')}</Button>{reason && <Text fontSize="sm" color="fg.muted">{translate(reasons, reason)}</Text>}</Stack>;
    }} />
  </Stack>;
}
