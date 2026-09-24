'use client';

import { Box, Button, HStack, Spinner, Text, VStack } from '@chakra-ui/react';
import { LuCheck, LuCircleAlert, LuClock, LuPause } from 'react-icons/lu';
import { useGamepadConfig } from '@/contexts/gamepad-config-context';
import { useLanguage } from '@/contexts/language-context';

export function useConfigSyncStatus() {
    const { currentLanguage } = useLanguage();
    const zh = currentLanguage === 'zh';
    const { configSyncState: state, configReadProgress, dataIsReady, deviceConnected, retrySync, configRecovery } = useGamepadConfig();
    const reading = deviceConnected && !dataIsReady;
    const paused = state.paused || !deviceConnected || configRecovery.length > 0;
    const status = reading ? 'reading' : !deviceConnected ? 'disconnected' : state.error ? 'error'
        : state.saving ? 'saving' : paused ? 'paused' : state.pendingCount ? 'pending' : 'saved';
    const text = status === 'reading' ? `${zh ? '正在读取配置' : 'Reading configuration'} ${configReadProgress.completed}/${configReadProgress.total || '…'}`
        : status === 'disconnected' ? (zh ? '设备未连接 · 同步暂停' : 'Disconnected · sync paused')
        : status === 'error' ? (zh ? '更改尚未保存' : 'Changes not saved')
        : status === 'saving' ? `${zh ? '正在保存到设备' : 'Saving to device'}${state.pendingCount ? ` (${state.pendingCount})` : ''}`
        : status === 'paused' ? (zh ? '等待设备操作或草稿确认' : 'Waiting for device or draft review')
        : status === 'pending' ? (zh ? '有未保存更改' : 'Unsaved changes')
        : (zh ? '所有更改已保存到设备' : 'All changes saved to device');
    const background = {
        reading: 'blue.700',
        disconnected: 'gray.700',
        error: 'red.700',
        saving: 'teal.700',
        paused: 'orange.700',
        pending: 'yellow.500',
        saved: 'green.700',
    }[status];
    const Icon = status === 'error' ? LuCircleAlert : status === 'paused' || status === 'disconnected' ? LuPause
        : status === 'pending' ? LuClock : LuCheck;
    return { status, text, background, foreground: status === 'pending' ? 'gray.900' : 'white', Icon,
        busy: status === 'reading' || status === 'saving', error: state.error,
        canRetry: status === 'error' && !paused, retrySync, zh };
}

export function ConfigSyncStatus() {
    const { status, text, background, foreground, Icon, busy, error, canRetry, retrySync, zh } = useConfigSyncStatus();
    return <HStack data-testid="config-sync-status" data-sync-state={status} role="status" aria-live="polite"
        width="230px" flexShrink={0} gap={2} px={3} py={1.5} minH="36px" borderRadius="md"
        bg={background} color={foreground}
        title={error ?? (zh ? '后台保存时，硬件预览可能短暂停顿。' : 'Hardware preview may pause briefly during background saves.')}>
        {busy ? <Spinner size="xs" /> : <Box as={Icon} flexShrink={0} />}
        <Text fontSize="xs" lineHeight="shorter" flex={1}>{text}</Text>
        {canRetry && <Button size="xs" variant="ghost" color="white" _hover={{ bg: 'whiteAlpha.200' }} onClick={retrySync}>{zh ? '重试' : 'Retry'}</Button>}
    </HStack>;
}

export function ConfigDraftRecovery() {
    const { currentLanguage } = useLanguage();
    const zh = currentLanguage === 'zh';
    const { configRecovery, resolveConfigRecovery } = useGamepadConfig();
    if (!configRecovery.length) return null;
    return <Box position="fixed" inset={0} bg="blackAlpha.700" zIndex={9500} display="grid" placeItems="center">
        <VStack role="dialog" aria-modal="true" aria-labelledby="draft-recovery-title" align="stretch" gap={4} p={6} borderRadius="lg" bg="bg.panel" maxWidth="640px" maxHeight="80vh" overflow="auto">
            <Text id="draft-recovery-title" fontWeight="bold">{zh ? '发现此设备尚未保存的修改' : 'Unsaved changes for this device'}</Text>
            <Text fontSize="sm">{zh ? '设备配置已重新读取。恢复会保留你的修改，其他字段采用设备当前值。' : 'The device has been read again. Restore your edits while keeping other current device values.'}</Text>
            {configRecovery.map((key, i) => <Text key={i} fontSize="xs" whiteSpace="pre-wrap" overflowWrap="anywhere">{key}</Text>)}
            <HStack justify="flex-end">
                <Button autoFocus variant="outline" onClick={() => resolveConfigRecovery(false)}>{zh ? '采用设备配置' : 'Use device configuration'}</Button>
                <Button colorPalette="green" onClick={() => resolveConfigRecovery(true)}>{zh ? '恢复本地修改' : 'Restore local changes'}</Button>
            </HStack>
        </VStack>
    </Box>;
}
