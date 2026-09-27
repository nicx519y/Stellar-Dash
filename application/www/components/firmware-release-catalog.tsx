'use client';
import { useCallback, useEffect, useRef, useState } from 'react';
import { Badge, Box, Button, Heading, HStack, Input, Stack, Text } from '@chakra-ui/react';
import { adminRuntime } from '@hbox/admin-runtime';
import { useLanguage } from '@/contexts/language-context';
import type { PublicFirmwareRelease, ReleasePage } from '@/lib/admin/firmware-types';
import { FirmwareReleaseDetails } from './firmware-release-details';

export function FirmwareReleaseCatalog() {
  const { currentLanguage } = useLanguage(); const zh = currentLanguage === 'zh';
  const [page, setPage] = useState<ReleasePage<PublicFirmwareRelease>>({ items: [], total: 0, limit: 20, offset: 0 });
  const [query, setQuery] = useState(''); const [hardware, setHardware] = useState(''); const [offset, setOffset] = useState(0);
  const [open, setOpen] = useState<string | null>(null); const [loading, setLoading] = useState(false); const [error, setError] = useState('');
  const generation = useRef(0);
  const load = useCallback(async () => {
    const g = ++generation.current; setLoading(true); setError('');
    try { const r = await adminRuntime.firmware.catalog({ query, hardware, offset }); if (g === generation.current) setPage(r); }
    catch (e) { if (g === generation.current) { setPage({ items: [], total: 0, limit: 20, offset: 0 }); setError(e instanceof Error ? e.message : String(e)); } }
    finally { if (g === generation.current) setLoading(false); }
  }, [query, hardware, offset]);
  useEffect(() => { void load(); return () => { generation.current++; }; }, [load]);
  return <Stack gap="4" w="full">
    <Heading size="xl">{zh ? 'XORA 固件目录' : 'XORA Firmware Catalog'}</Heading>
    <Text color="fg.muted">{zh ? '浏览已发布的整机版本、组件和更新说明。此目录仅供浏览，不执行设备升级。' : 'Browse published releases, components and release notes. This catalog does not install firmware.'}</Text>
    {process.env.NEXT_PUBLIC_OFFLINE_PREVIEW === 'true' && <Badge alignSelf="start" colorPalette="purple">MOCK PREVIEW</Badge>}
    <HStack wrap="wrap"><Input maxW="280px" aria-label={zh ? '搜索固件' : 'Search firmware'} placeholder={zh ? '搜索版本或更新说明' : 'Search versions or release notes'} value={query} onChange={e => { setQuery(e.target.value); setOffset(0); }} /><Input maxW="190px" aria-label={zh ? '硬件版本' : 'Hardware version'} placeholder={zh ? '硬件版本，如 2.0.0' : 'Hardware, e.g. 2.0.0'} value={hardware} onChange={e => { setHardware(e.target.value); setOffset(0); }} /><Button variant="surface" loading={loading} onClick={() => void load()}>{zh ? '刷新' : 'Refresh'}</Button></HStack>
    {error && <Text role="alert" color="red.500">{error}</Text>}
    {!error && !loading && !page.items.length && <Box borderWidth="1px" borderRadius="lg" p="6"><Text>{zh ? '暂无匹配的正式固件。' : 'No matching published firmware.'}</Text></Box>}
    {page.items.map(r => <Box key={r.id} borderWidth="1px" borderColor="app.border" borderRadius="xl" p="5"><Stack gap="3">
      <HStack justify="space-between" wrap="wrap"><Heading size="md">XORA {r.manifest.version}</Heading><Badge colorPalette="green">{zh ? '正式发布' : 'Published'}</Badge></HStack>
      <Text fontSize="sm" color="fg.muted">{zh ? '硬件' : 'Hardware'} {r.manifest.hardwareVersion} · {r.publishedAt ? new Date(r.publishedAt).toLocaleString() : ''}</Text>
      <Text whiteSpace="pre-wrap" overflowWrap="anywhere">{r.notes}</Text>
      <Button alignSelf="start" variant="surface" aria-expanded={open === r.id} onClick={() => setOpen(open === r.id ? null : r.id)}>{open === r.id ? (zh ? '收起组件详情' : 'Hide components') : (zh ? '查看组件详情' : 'View components')}</Button>
      {open === r.id && <FirmwareReleaseDetails manifest={r.manifest} zh={zh} />}
    </Stack></Box>)}
    <HStack><Button size="sm" disabled={offset === 0 || loading} onClick={() => setOffset(Math.max(0, offset - 20))}>{zh ? '上一页' : 'Previous'}</Button><Text>{page.total} {zh ? '个版本' : 'releases'}</Text><Button size="sm" disabled={offset + page.limit >= page.total || loading} onClick={() => setOffset(offset + 20)}>{zh ? '下一页' : 'Next'}</Button></HStack>
  </Stack>;
}
