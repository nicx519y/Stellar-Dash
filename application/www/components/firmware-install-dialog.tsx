'use client';
import { ExclusiveDialog } from '@/components/ui/exclusive-dialog';
import { OVERLAY_PRIORITY } from '@/lib/overlay-coordinator';
import { useEffect, useId, useState } from 'react';
import { Box, Button, Dialog, Flex, HStack, Portal, Stack, Text } from '@chakra-ui/react';
import type { PublicFirmwareRelease } from '@/lib/admin/firmware-types';
import type { ReleaseProgress, TxIapTransferMode } from '@/lib/device-transport/release-install-client';
import { normalizeTxIapMode, txIapModeLabel } from '@/lib/device-transport/release-install-transfer';
import { displayedInstallPercent, installUsesEstimate, INSTALL_DISPLAY_STEPS, installDisplayStep } from '@/lib/device-transport/release-install-progress';
import styles from './firmware-install-dialog.module.css';

const circumference = 2 * Math.PI * 110;

function ProgressRing({ progress, percent, label, zh }: {
  progress: ReleaseProgress | null; percent: number; label: string; zh: boolean;
}) {
  const gradientId = `install-gradient-${useId().replace(/:/g, '')}`;
  const step = installDisplayStep(progress);
  return <Box position="relative" w="clamp(160px, calc(100dvh - 420px), 240px)" maxW="full" aspectRatio="1" flexShrink={0}
    css={{ '@media (prefers-reduced-motion: reduce)': { '& circle': { transition: 'none' } } }}>
    <svg width="100%" height="100%" viewBox="0 0 240 240" role="progressbar"
      aria-label={zh ? '安装总进度' : 'Overall installation progress'} aria-valuemin={0}
      aria-valuemax={100} aria-valuenow={percent}
      aria-valuetext={`${percent}% · ${label}`}>
      <defs>
        <linearGradient id={gradientId} x1="0%" y1="0%" x2="100%" y2="100%">
          <stop className={styles.gradientStop} offset="0%" stopColor="#83e5b0" />
          <stop className={styles.gradientStop} offset="38%" stopColor="#b6f0b0" style={{ animationDelay: '-4s' }} />
          <stop className={styles.gradientStop} offset="67%" stopColor="#b4a0f5" style={{ animationDelay: '-8s' }} />
          <stop className={styles.gradientStop} offset="100%" stopColor="#9366e8" style={{ animationDelay: '-12s' }} />
        </linearGradient>
      </defs>
      <circle cx="120" cy="120" r="110" fill="none" stroke="rgba(174, 189, 207, 0.12)" strokeWidth="5" />
      <circle cx="120" cy="120" r="110" fill="none" stroke={`url(#${gradientId})`} strokeWidth="5" strokeLinecap="round"
        strokeDasharray={circumference}
        strokeDashoffset={circumference * (1 - percent / 100)}
        transform="rotate(-90 120 120)"
        style={{ transition: 'stroke-dashoffset 950ms ease', filter: 'drop-shadow(0 0 4px rgba(170, 140, 235, .26))' }} />
    </svg>
    <Flex position="absolute" inset="0" align="center" justify="center" direction="column" pointerEvents="none">
      <Text fontSize="42px" lineHeight="1.1" fontWeight="semibold" color="#daf4d7" fontVariantNumeric="tabular-nums">
        {percent}%
      </Text>
      <Box data-testid="install-step-timeline" data-step={step} position="relative" w="160px" h="40px" mt="1.5"
        overflow="hidden" aria-label={zh ? '升级阶段' : 'Installation stage'}
        style={{ maskImage: 'linear-gradient(to right, transparent, black 12%, black 88%, transparent)' }}>
        <Flex position="absolute" left="50%" top="0" h="full" w="480px"
          transform={`translateX(${-80 - step * 160}px)`}
          transition="transform 580ms cubic-bezier(.22,1,.36,1)" _motionReduce={{ transition: 'none' }}>
          {INSTALL_DISPLAY_STEPS.map((item, index) => <Flex key={item.en} w="160px" flexShrink={0} align="flex-start" justify="center"
            opacity={index === step ? 1 : 0} transition="opacity 580ms ease" _motionReduce={{ transition: 'none' }}>
            <Text fontSize="12px" lineHeight="1.5" textAlign="center" maxW="132px" color="#d7dfda"
              aria-hidden={index !== step}>{zh ? item.zh : item.en}</Text>
          </Flex>)}
        </Flex>
      </Box>
    </Flex>
  </Box>;
}

export function FirmwareInstallDialog({ release, zh, view, progress, error, busy, retryable,
  progressLabel, onClose, onConfirm, onRetry, onReconnect, canReconnect, deviceConnected, activatedAt, txRecoveryMode }: {
  release: PublicFirmwareRelease | null;
  zh: boolean;
  view: 'confirm' | 'progress';
  progress: ReleaseProgress | null;
  error: string;
  busy: boolean;
  retryable: boolean;
  progressLabel: string;
  onReconnect: () => void;
  canReconnect: boolean;
  deviceConnected: boolean;
  activatedAt?: number;
  txRecoveryMode?: TxIapTransferMode;
  onClose: () => void;
  onConfirm: () => void;
  onRetry: () => void;
}) {
  const displayKey = `${release?.id || ''}:${activatedAt || 0}`;
  const [display, setDisplay] = useState({ key: displayKey, percent: 0 });
  const estimated = busy && installUsesEstimate(progress, activatedAt);
  useEffect(() => {
    const update = () => setDisplay(previous => {
      const percent = displayedInstallPercent(progress, activatedAt, Date.now(), previous.key === displayKey ? previous.percent : 0);
      return previous.key === displayKey && previous.percent === percent ? previous : { key: displayKey, percent };
    });
    update();
    if (!release || view !== 'progress' || !estimated) return;
    const timer = setInterval(update, 1000);
    return () => clearInterval(timer);
  }, [release, view, estimated, progress, activatedAt, displayKey]);
  const percent = displayedInstallPercent(progress, undefined, Date.now(), display.key === displayKey ? display.percent : 0);
  const locked = view === 'progress' && busy;
  // A pending/failed device-owned transaction keeps its recovery status visible.
  // A completed result or unopened confirmation yields to ordinary disconnect UI.
  const ownsConnection = view === 'progress' && (busy || (canReconnect && progress?.stage !== 'completed'));
  const exceptional = Boolean(error) || ['completed', 'timeout', 'restored', 'restore-failed', 'failed', 'restoring', 'tx-restoring', 'rollback-verifying', 'rollback-committing'].includes(progress?.stage || '');
  const statusLabel = progress?.stage === 'completed' ? (zh ? '升级完成' : 'Update complete') : exceptional ? progressLabel : (zh
    ? INSTALL_DISPLAY_STEPS[installDisplayStep(progress)].zh : INSTALL_DISPLAY_STEPS[installDisplayStep(progress)].en);
  return <Portal><ExclusiveDialog priority={ownsConnection ? OVERLAY_PRIORITY.operation : OVERLAY_PRIORITY.editor} open={Boolean(release)} closeOnEscape={!locked} closeOnInteractOutside={!locked}
    onOpenChange={details => { if (!details.open && !locked) onClose(); }}>
    <Dialog.Backdrop backdropFilter="blur(4px)" />
    <Dialog.Positioner p="12px" pt="var(--install-dialog-top)" alignItems="flex-start"
      css={{ '--install-dialog-top': '12px', '@media (min-height: 760px)': { '--install-dialog-top': 'clamp(24px, 8dvh, 120px)' } }}>
      <Dialog.Content w="min(420px, calc(100vw - 24px))" my="0"
        h="auto"
        maxH="calc(100dvh - var(--install-dialog-top) - 12px)" overflow="hidden"
        transition="width 340ms cubic-bezier(.22,1,.36,1) 140ms, height 340ms cubic-bezier(.22,1,.36,1) 140ms"
        _motionReduce={{ transition: 'none' }}>
        <Dialog.Header flexShrink={0} borderBottomWidth="1px" py="4">
          <Dialog.Title fontSize="18px">{release ? `${zh ? '安装' : 'Install'} XORA ${release.manifest.version}` : ''}</Dialog.Title>
        </Dialog.Header>
        <Dialog.Body p="0" flex="1" minH="0" position="relative">
          <Flex display={view === 'confirm' ? 'flex' : 'none'} direction="column" p="6" gap="6" aria-hidden={view !== 'confirm'} inert={view !== 'confirm'}
            opacity={view === 'confirm' ? 1 : 0} pointerEvents={view === 'confirm' ? 'auto' : 'none'}
            transition={view === 'confirm' ? 'opacity 180ms ease 150ms' : 'opacity 140ms ease'}
            _motionReduce={{ transition: 'none' }}>
            <Stack gap="3" flex="1" fontSize="14px">
              <Text>{zh ? '将安装此版本的主控和 TX 固件。RX 固件单独更新。' : 'This installs the controller and TX firmware in this release. RX updates are separate.'}</Text>
              <Text color="fg.muted">{zh ? '确认后会下载并验证发布包、保存配置备份，然后开始安装。TX 更新期间请保持供电。' : 'After confirmation, WebConfig verifies the package, backs up your settings, then starts installation. Keep the device powered during the TX update.'}</Text>
            </Stack>
            <HStack justify="flex-end" gap="2" flexShrink={0}>
              <Button h="36px" fontSize="14px" variant="surface" onClick={onClose}>{zh ? '取消' : 'Cancel'}</Button>
              <Button h="36px" fontSize="14px" colorPalette="green" onClick={onConfirm}>{zh ? '确认安装' : 'Confirm installation'}</Button>
            </HStack>
          </Flex>
          <Flex display={view === 'progress' ? 'flex' : 'none'} direction="column" align="center" justify="center" px="6" py="4"
            css={{ '@media (min-height: 760px)': { minHeight: '490px' } }}
            aria-hidden={view !== 'progress'} inert={view !== 'progress'} opacity={view === 'progress' ? 1 : 0}
            pointerEvents={view === 'progress' ? 'auto' : 'none'}
            transition={view === 'progress' ? 'opacity 180ms ease 100ms' : 'opacity 120ms ease'}
            _motionReduce={{ transition: 'none' }}>
            <ProgressRing progress={progress} percent={percent} label={statusLabel} zh={zh} />
            <Stack w="full" maxW="320px" gap="1.5" mt="22px" align="center" textAlign="center">
            <Text fontSize="16px" lineHeight="1.5" fontWeight="semibold" role="status" aria-live="polite">
              {exceptional ? statusLabel : (zh ? '请保持供电，勿断电或关机' : 'Keep the device powered on')}
            </Text>
            {!deviceConnected && !busy && <Text role="alert" fontSize="13px" lineHeight="1.6" color="orange.300">
              {zh ? '设备连接已断开。本次升级结果已保留；请检查供电、USB 连接及 WebConfig 模式后重新连接。'
                : 'Device disconnected. The installation result is retained. Check power, USB and WebConfig mode, then reconnect.'}
            </Text>}
            {error && <Text role="alert" fontSize="13px" color="red.400" lineHeight="1.6" overflowWrap="anywhere">{error}</Text>}
            {!error && <Text fontSize="13px" color="fg.muted" lineHeight="1.6">
              {busy
                ? (zh ? '请勿拔掉 USB 或关闭设备。连接会暂时中断，完成后将自动重新连接。' : 'Do not unplug USB or turn off the device. It will reconnect automatically after the update.')
                : progress?.stage === 'completed'
                  ? (zh ? '升级完成，可以关闭此窗口。' : 'Update complete. You can close this window.')
                  : (zh ? '请检查设备状态后继续。' : 'Check the device status before continuing.')}
            </Text>}
            {(normalizeTxIapMode(txRecoveryMode) !== 'unknown' || ['restored', 'restore-failed'].includes(progress?.stage || '')) && <Text data-testid="tx-recovery-mode" fontSize="12px" color="fg.muted">
              {zh ? 'TX 恢复：' : 'TX recovery: '}{txIapModeLabel(txRecoveryMode, zh)}
            </Text>}
            </Stack>
            <HStack w="full" justify="center" gap="2" mt={busy ? '0' : '4'} flexShrink={0} wrap="wrap">
              {canReconnect && !busy && (!deviceConnected || progress?.stage !== 'completed') && <Button h="36px" fontSize="14px" variant="surface" onClick={onReconnect}>{zh ? '重新连接设备' : 'Reconnect device'}</Button>}
              {!busy && <Button h="36px" fontSize="14px" variant="surface" onClick={onClose}>{zh ? '关闭' : 'Close'}</Button>}
              {error && !busy ? <>
                {retryable && <Button h="36px" fontSize="14px" colorPalette="green" onClick={onRetry}>{zh ? '重试安装' : 'Retry installation'}</Button>}
              </> : null}
            </HStack>
          </Flex>
        </Dialog.Body>
      </Dialog.Content>
    </Dialog.Positioner>
  </ExclusiveDialog></Portal>;
}
