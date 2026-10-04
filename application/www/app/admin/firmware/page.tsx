'use client';
import { useCallback, useEffect, useRef, useState } from 'react';
import type { DragEvent } from 'react';
import { Badge, Box, Button, Center, Flex, Heading, HStack, Input, NativeSelect, Progress, Spinner, Stack, Text } from '@chakra-ui/react';
import { adminRuntime } from '@hbox/admin-runtime';
import { useLanguage } from '@/contexts/language-context';
import { useUserAuth } from '@/contexts/user-auth-context';
import Link from 'next/link';
import { useRouter } from 'next/navigation';
import { LuArrowUpRight, LuPackage, LuTrash2, LuUpload } from 'react-icons/lu';
import { AdminCard, AdminPageHeader } from '@/components/admin/admin-surface';
import { AdminConfirmDialog } from '@/components/admin/admin-confirm-dialog';
import { Alert } from '@/components/ui/alert';
import type { FirmwareRelease, LegacyFirmware } from '@/lib/admin/firmware-types';
import { loadAllFirmwareReleases } from '@/lib/admin/firmware-list';

const api = adminRuntime.firmware;
export default function AdminFirmwarePage() {
  const router = useRouter();
  const { currentLanguage } = useLanguage(); const zh = currentLanguage === 'zh';
  const { session, loading: authLoading } = useUserAuth();
  const isAdmin = session.authenticated && session.user?.role === 'admin';
  const [releases, setReleases] = useState<FirmwareRelease[]>([]);
  const [legacy, setLegacy] = useState<LegacyFirmware[]>([]);
  const [query, setQuery] = useState(''); const [status, setStatus] = useState('');
  const [loading, setLoading] = useState(false);
  const [pendingDelete, setPendingDelete] = useState<FirmwareRelease | null>(null);
  const deleting = useRef(false);
  const [busy, setBusy] = useState(''); const [error, setError] = useState(''); const [message, setMessage] = useState('');
  const [progress, setProgress] = useState(0); const input = useRef<HTMLInputElement>(null);
  const [dragActive, setDragActive] = useState(false); const dragDepth = useRef(0);
  const generation = useRef(0);
  const statusName = (s: string) => ({ draft: zh ? '草稿' : 'Draft', published: zh ? '已发布' : 'Published', withdrawn: zh ? '已撤回' : 'Withdrawn' }[s] || s);
  const load = useCallback(async () => {
    if (!isAdmin) return;
    const request = ++generation.current; setLoading(true);
    try {
      const result = await loadAllFirmwareReleases(api.list, { query, status }, () => request === generation.current);
      if (result && request === generation.current) setReleases(result);
    } catch (e) { if (request === generation.current) setError(e instanceof Error ? e.message : String(e)); }
    finally { if (request === generation.current) setLoading(false); }
  }, [isAdmin, query, status]);
  useEffect(() => { void load(); return () => { generation.current++; }; }, [load]);
  useEffect(() => { if (!isAdmin) { setReleases([]); setLegacy([]); } }, [isAdmin]);
  async function act(name: string, operation: () => Promise<void>) {
    setBusy(name); setError(''); setMessage('');
    try { await operation(); } catch (e) { setError(e instanceof Error ? e.message : String(e)); }
    finally { setBusy(''); }
  }
  async function upload(file?: File) {
    if (!file) return;
    await act('import', async () => {
      if (!file.name.toLowerCase().endsWith('.zip')) throw new Error(zh ? '请选择 ZIP 发布包' : 'Choose a ZIP release package');
      if (file.size > 12 * 1024 * 1024) throw new Error(zh ? '发布包不能超过 12 MiB' : 'Package cannot exceed 12 MiB');
      setProgress(0);
      const result = await api.importBundle(file, setProgress);
      if (result.status !== 'completed' || !result.releaseId) throw new Error(result.error || 'Import failed');
      router.push(`/admin/firmware/detail/?id=${encodeURIComponent(result.releaseId)}`);
    });
  }
  async function deleteRelease() {
    if (!pendingDelete || busy || deleting.current || !isAdmin) return;
    const target = pendingDelete;
    deleting.current = true;
    setPendingDelete(null);
    try {
      await act(`delete:${target.id}`, async () => {
        await api.remove(target.id, target.revision);
        generation.current++;
        setReleases(items => items.filter(item => item.id !== target.id));
        setMessage(zh ? `已删除 XORA ${target.manifest.version}。` : `Deleted XORA ${target.manifest.version}.`);
        await load();
      });
    } finally { deleting.current = false; }
  }
  const hasDraggedFiles = (event: DragEvent<HTMLDivElement>) => Array.from(event.dataTransfer.types).includes('Files');
  function onDragEnter(event: DragEvent<HTMLDivElement>) {
    if (!hasDraggedFiles(event) && dragDepth.current === 0) return;
    event.preventDefault();
    dragDepth.current++;
    if (!busy) setDragActive(true);
  }
  function onDragOver(event: DragEvent<HTMLDivElement>) {
    if (!hasDraggedFiles(event) && dragDepth.current === 0) return;
    event.preventDefault();
    event.dataTransfer.dropEffect = busy ? 'none' : 'copy';
  }
  function onDragLeave(event: DragEvent<HTMLDivElement>) {
    if (dragDepth.current === 0) return;
    event.preventDefault();
    dragDepth.current = Math.max(0, dragDepth.current - 1);
    if (dragDepth.current === 0) setDragActive(false);
  }
  function onDrop(event: DragEvent<HTMLDivElement>) {
    if (!hasDraggedFiles(event) && event.dataTransfer.files.length === 0 && dragDepth.current === 0) return;
    event.preventDefault();
    dragDepth.current = 0;
    setDragActive(false);
    if (busy) return;
    if (event.dataTransfer.files.length !== 1) {
      setMessage('');
      setError(zh ? '请一次拖入一个 ZIP 发布包' : 'Drop one ZIP release package at a time');
      return;
    }
    void upload(event.dataTransfer.files[0]);
  }
  return <>
    <AdminPageHeader title={zh ? '固件管理' : 'Firmware'}
      description={zh ? '管理发布包与更新说明，将已验收的版本发布到固件目录。' : 'Manage release packages and notes, and publish verified versions to the catalog.'}
      actions={<Button asChild variant="surface" flexShrink={0}><Link href="/firmware/releases/">{zh ? '浏览固件目录' : 'Browse catalog'}<LuArrowUpRight /></Link></Button>} />
    {authLoading ? <Center minH="320px"><Spinner size="lg" colorPalette="green" /></Center> : !isAdmin ? <Alert colorPalette="orange" title={zh ? '请使用管理员账户登录。' : 'Sign in with an administrator account.'} /> : <>
      {process.env.NEXT_PUBLIC_OFFLINE_PREVIEW === 'true' && <Box bg="purple.subtle" color="purple.fg" borderRadius="lg" p="3" fontSize="sm">{zh ? 'MOCK 预览：数据仅存于本标签页，不执行真实签名校验或发布。' : 'MOCK PREVIEW: tab-local data, no real signature verification or publishing.'}</Box>}
      {error && <Box role="alert" p="3" borderWidth="1px" borderColor="red.500" overflowWrap="anywhere">{error}</Box>}
      {message && <Text role="status" color="green.600">{message}</Text>}
      <AdminCard><Stack gap="4">
        <HStack><LuUpload /><Heading size="lg">{zh ? '导入发布包' : 'Import release package'}</Heading></HStack>
        <Text fontSize="sm" color="fg.muted">{zh ? '签名 ZIP · 最大 12 MiB · 包含主控 A/B、TX 和可选 RX。上传不会自动发布。' : 'Signed ZIP · up to 12 MiB · main controller A/B, TX and optional RX. Uploads are never published automatically.'}</Text>
        <Box borderWidth="2px" borderStyle="dashed" borderColor={dragActive ? 'green.500' : 'app.border'}
          bg={dragActive ? 'green.subtle' : 'app.canvas'} borderRadius="lg" p={{ base: '6', md: '8' }} textAlign="center"
          onDragEnter={onDragEnter} onDragOver={onDragOver} onDragLeave={onDragLeave} onDrop={onDrop}>
          <Stack align="center" gap="3">
            <LuUpload aria-hidden="true" size={28} />
            <Text fontWeight="medium">{dragActive ? (zh ? '松开鼠标即可上传' : 'Release to upload') : (zh ? '将 ZIP 发布包拖到这里' : 'Drop a ZIP release package here')}</Text>
            <Text fontSize="sm" color="fg.muted">{zh ? '也可以从电脑中选择文件' : 'Or choose a file from your computer'}</Text>
            <Button colorPalette="green" disabled={!!busy} onClick={() => input.current?.click()}>{zh ? '选择发布包' : 'Choose release package'}</Button>
            <input ref={input} hidden type="file" accept=".zip" onChange={e => { const file = e.target.files?.[0]; e.target.value = ''; void upload(file); }} />
          </Stack>
        </Box>
        {busy === 'import' && <><Text role="status">{progress < 100 ? `${zh ? '上传中' : 'Uploading'} ${progress}%` : (zh ? '服务器正在验签及检查组件…' : 'Server is verifying signatures and components…')}</Text><Progress.Root value={progress < 100 ? progress : null}><Progress.Track><Progress.Range /></Progress.Track></Progress.Root></>}
      </Stack></AdminCard>
      <AdminCard><Stack gap="5">
      <HStack><LuPackage /><Heading size="lg">{zh ? '发布列表' : 'Releases'}</Heading><Badge variant="subtle">{releases.length}</Badge></HStack>
      <Flex gap="3" wrap="wrap"><Input maxW="340px" aria-label={zh ? '搜索版本或说明' : 'Search versions or notes'} placeholder={zh ? '搜索版本或更新说明' : 'Search versions or notes'} value={query} onChange={e => setQuery(e.target.value)} />
        <NativeSelect.Root maxW="200px"><NativeSelect.Field aria-label={zh ? '发布状态' : 'Release status'} value={status} onChange={e => setStatus(e.target.value)}><option value="">{zh ? '全部状态' : 'All states'}</option>{['draft', 'published', 'withdrawn'].map(s => <option key={s} value={s}>{statusName(s)}</option>)}</NativeSelect.Field><NativeSelect.Indicator /></NativeSelect.Root>
        <Button variant="surface" disabled={!!busy} loading={loading} onClick={() => void load()}>{zh ? '刷新列表' : 'Refresh'}</Button></Flex>
      <Stack gap="2">{releases.map(r => <Flex key={r.id} p="4" borderWidth="1px" borderColor="app.border" borderRadius="lg" bg="app.canvas" align="center" justify="space-between" gap="3" wrap="wrap">
        <Flex asChild minW="0" flex={{ base: '1 1 100%', md: '1' }} direction={{ base: 'column', md: 'row' }} gap="3" borderRadius="md" _hover={{ color: 'green.fg' }} _focusVisible={{ outline: '2px solid', outlineColor: 'green.500' }}><Link href={`/admin/firmware/detail/?id=${encodeURIComponent(r.id)}`}>
        <Stack gap="1" minW="0" flex="1"><HStack flexWrap="wrap"><Text fontWeight="bold">XORA {r.manifest.version}</Text><Badge colorPalette={r.status === 'published' ? 'green' : r.status === 'withdrawn' ? 'orange' : 'gray'}>{statusName(r.status)}</Badge></HStack>
          <Text fontSize="sm" color="fg.muted" overflowWrap="anywhere">{zh ? '硬件' : 'Hardware'} {r.manifest.hardwareVersion} · {r.manifest.artifacts.map(a => `${a.component === 'stm32' ? (zh ? '主控' : 'Main controller') : a.component.toUpperCase()}${a.slot ? ` ${a.slot}` : ''} ${a.version}`).join(' / ')}</Text>
          <Text fontSize="xs">{r.publishedAt ? `${zh ? '发布于' : 'Published'} ${new Date(r.publishedAt).toLocaleString()}` : `${zh ? '创建于' : 'Created'} ${new Date(r.createdAt).toLocaleString()}`}</Text>
        </Stack><Text alignSelf={{ base: 'flex-start', md: 'center' }} flexShrink={0} color="green.fg" fontSize="sm" fontWeight="medium">{zh ? '查看详情 →' : 'Details →'}</Text>
        </Link></Flex>
        <Button size="sm" variant="outline" colorPalette="red" disabled={!!busy} loading={busy === `delete:${r.id}`} aria-label={zh ? `删除 XORA ${r.manifest.version}` : `Delete XORA ${r.manifest.version}`} onClick={() => setPendingDelete(r)}><LuTrash2 />{zh ? '删除固件' : 'Delete firmware'}</Button>
      </Flex>)}{!loading && releases.length === 0 && <Text color="fg.muted" py="8" textAlign="center">{zh ? '暂无匹配的发布。' : 'No matching releases.'}</Text>}</Stack>
      </Stack></AdminCard>
      <AdminCard><Button variant="surface" whiteSpace="normal" height="auto" minH="10" py="2" disabled={!!busy} onClick={() => void act('legacy', async () => { const items = await api.legacy(); setLegacy(items); if (!items.length) setMessage(zh ? '没有历史 STM32 发布。' : 'No historical STM32 releases.'); })}>{zh ? '查看历史 STM32 发布（只读）' : 'Historical STM32 releases (read only)'}</Button>
        {legacy.map(r => <Text key={r.id} mt="2">STM32 {r.version} · {r.hardwareVersion} · {r.notes}</Text>)}</AdminCard>
    </>}
    <AdminConfirmDialog open={isAdmin && pendingDelete !== null} onClose={() => setPendingDelete(null)} onConfirm={() => void deleteRelease()}
      title={zh ? '确认删除固件' : 'Confirm firmware deletion'}
      description={<Text>{zh ? `删除 XORA ${pendingDelete?.manifest.version}？删除后将从管理列表和公开目录移除，无法再下载。此操作不可恢复；审计记录和包文件仍会保留，已下载或安装的固件不受影响。` : `Delete XORA ${pendingDelete?.manifest.version}? It will be removed from the admin list and public catalog and will no longer be downloadable. This cannot be undone. Audit records and package files are retained; existing downloads and installations are unaffected.`}</Text>}
      cancelLabel={zh ? '取消' : 'Cancel'} confirmLabel={zh ? '确认删除' : 'Delete firmware'} confirmColorPalette="red" />
  </>;
}
