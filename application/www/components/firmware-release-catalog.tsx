'use client';
import { useCallback, useEffect, useRef, useState } from 'react';
import { Badge, Box, Button, Flex, Heading, HStack, Input, Stack, Text } from '@chakra-ui/react';
import { adminRuntime } from '@hbox/admin-runtime';
import { useLanguage } from '@/contexts/language-context';
import type { PublicFirmwareRelease, ReleasePage } from '@/lib/admin/firmware-types';
import { displayFirmwareNotes, FirmwareReleaseInfo, firmwareNotesStyle } from './firmware-release-info';

export interface FirmwareCatalogAction {
  label: string;
  disabled: boolean;
  reason?: string;
  onClick: () => void;
}

export function FirmwareReleaseCatalog({ getReleaseAction }: {
  getReleaseAction?: (release: PublicFirmwareRelease) => FirmwareCatalogAction;
} = {}) {
  const { currentLanguage } = useLanguage(); const zh = currentLanguage === 'zh';
  const [page, setPage] = useState<ReleasePage<PublicFirmwareRelease>>({ items: [], total: 0, limit: 20, offset: 0 });
  const [query, setQuery] = useState(''); const [hardware, setHardware] = useState(''); const [offset, setOffset] = useState(0);
  const [loading, setLoading] = useState(false); const [error, setError] = useState('');
  const generation = useRef(0);
  const load = useCallback(async () => {
    const g = ++generation.current; setLoading(true); setError('');
    try { const next = await adminRuntime.firmware.catalog({ query, hardware, offset }); if (g === generation.current) setPage(next); }
    catch (e) { if (g === generation.current) { setPage({ items: [], total: 0, limit: 20, offset: 0 }); setError(e instanceof Error ? e.message : String(e)); } }
    finally { if (g === generation.current) setLoading(false); }
  }, [query, hardware, offset]);
  useEffect(() => { void load(); return () => { generation.current++; }; }, [load]);
  const selectForInstall = (action: FirmwareCatalogAction) => {
    if (action.disabled) return;
    action.onClick();
  };
  return <Stack gap="4" w="full" minW={0}>
    <HStack justify="space-between" align="baseline" wrap="wrap" gap="2">
      <Heading as="h2" fontSize="18px">{zh ? '可用版本' : 'Available releases'}</Heading>
      <Text fontSize="12px" color="fg.muted">{page.total} {zh ? '个版本' : page.total === 1 ? 'release' : 'releases'}</Text>
    </HStack>
    <Text fontSize="14px" color="fg.muted">{getReleaseAction
      ? (zh ? '选择兼容版本升级、降级或重新安装；RX 单独更新。' : 'Upgrade, downgrade or reinstall a compatible release. RX updates are separate.')
      : (zh ? '浏览已发布版本及更新说明。' : 'Browse published versions and release notes.')}</Text>
    {process.env.NEXT_PUBLIC_OFFLINE_PREVIEW === 'true' && <Badge alignSelf="start" colorPalette="purple">MOCK PREVIEW</Badge>}
    <Flex wrap="wrap" gap="2" align="center">
      <Input flex="1 1 250px" minW={0} maxW={{ md: '360px' }} h="36px" fontSize="14px"
        aria-label={zh ? '搜索固件' : 'Search firmware'} placeholder={zh ? '搜索版本或更新说明' : 'Search versions or release notes'}
        value={query} onChange={e => { setQuery(e.target.value); setOffset(0); }} />
      <Input flex="1 1 150px" minW={0} maxW={{ md: '200px' }} h="36px" fontSize="14px"
        aria-label={zh ? '硬件版本' : 'Hardware version'} placeholder={zh ? '硬件版本，如 2.0.0' : 'Hardware, e.g. 2.0.0'}
        value={hardware} onChange={e => { setHardware(e.target.value); setOffset(0); }} />
      <Button h="36px" fontSize="14px" variant="surface" loading={loading} onClick={() => void load()}>
        {zh ? '刷新版本列表' : 'Refresh releases'}
      </Button>
    </Flex>
    {error && <Text role="alert" fontSize="14px" color="red.500" overflowWrap="anywhere">{error}</Text>}
    {!error && !loading && !page.items.length && <Box borderWidth="1px" borderRadius="lg" p="6">
      <Text fontSize="14px">{zh ? '暂无匹配的正式固件。' : 'No matching published firmware.'}</Text>
    </Box>}
    <Stack gap="2">
      {page.items.map(release => {
        const action = getReleaseAction?.(release);
        return <Box key={release.id} borderWidth="1px" borderColor="app.border" borderRadius="lg" px={{ base: '4', md: '5' }} py="4" minW={0}>
          <Flex justify="space-between" align={{ base: 'stretch', md: 'center' }} direction={{ base: 'column', md: 'row' }} gap="4" minW={0}>
            <Stack gap="1" flex="1" minW={0}>
              <Heading as="h3" fontSize="16px">XORA {release.manifest.version}</Heading>
              <Text fontSize="12px" color="fg.muted">
                {zh ? '硬件' : 'Hardware'} {release.manifest.hardwareVersion}
                {release.publishedAt ? ` · ${new Date(release.publishedAt).toLocaleDateString(zh ? 'zh-CN' : 'en-US')}` : ''}
              </Text>
              <Text {...firmwareNotesStyle} color="fg.muted" lineClamp={2} whiteSpace="pre-wrap" overflowWrap="anywhere">
                {displayFirmwareNotes(release.notes, zh)}
              </Text>
            </Stack>
            <Stack gap="1" align={{ base: 'stretch', md: 'end' }} flexShrink={0}>
              {action && <Button h="36px" fontSize="14px" colorPalette="green" disabled={action.disabled}
                onClick={() => selectForInstall(action)}>{action.label}</Button>}
              {action?.reason && <Text fontSize="12px" color="fg.muted" maxW={{ md: '250px' }} textAlign={{ md: 'end' }}>
                {action.reason}
              </Text>}
            </Stack>
          </Flex>
          <Box as="details" fontSize="12px" color="fg.muted" mt="4" pt="2" borderTopWidth="1px">
            <Box as="summary" cursor="pointer" fontWeight="semibold">{zh ? '查看详情' : 'View details'}</Box>
            <Box mt="3">
              <FirmwareReleaseInfo release={release} zh={zh} />
            </Box>
          </Box>
        </Box>;
      })}
    </Stack>
    {page.total > page.limit && <HStack justify="space-between" wrap="wrap" gap="2" fontSize="12px" color="fg.muted">
      <Text>{zh ? `第 ${Math.floor(offset / page.limit) + 1} / ${Math.ceil(page.total / page.limit)} 页` : `Page ${Math.floor(offset / page.limit) + 1} of ${Math.ceil(page.total / page.limit)}`}</Text>
      <HStack gap="2">
        <Button h="36px" fontSize="14px" variant="surface" disabled={offset === 0 || loading}
          onClick={() => setOffset(Math.max(0, offset - page.limit))}>{zh ? '上一页' : 'Previous'}</Button>
        <Button h="36px" fontSize="14px" variant="surface" disabled={offset + page.limit >= page.total || loading}
          onClick={() => setOffset(offset + page.limit)}>{zh ? '下一页' : 'Next'}</Button>
      </HStack>
    </HStack>}
  </Stack>;
}
