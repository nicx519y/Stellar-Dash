'use client';
import { useCallback, useEffect, useRef, useState } from 'react';
import { Badge, Box, Button, Center, Flex, Heading, HStack, Input, NativeSelect, Progress, Spinner, Stack, Text, Textarea } from '@chakra-ui/react';
import { adminRuntime } from '@hbox/admin-runtime';
import { useLanguage } from '@/contexts/language-context';
import { useUserAuth } from '@/contexts/user-auth-context';
import Link from 'next/link';
import { LuArrowUpRight, LuPackage, LuUpload } from 'react-icons/lu';
import { AdminCard, AdminPageHeader } from '@/components/admin/admin-surface';
import { Alert } from '@/components/ui/alert';
import { FirmwareReleaseDetails } from '@/components/firmware-release-details';
import type { FirmwareRelease, LegacyFirmware, ReleasePage } from '@/lib/admin/firmware-types';

const api = adminRuntime.firmware;
export default function AdminFirmwarePage() {
  const { currentLanguage } = useLanguage(); const zh = currentLanguage === 'zh';
  const { session, loading: authLoading } = useUserAuth();
  const isAdmin = session.authenticated && session.user?.role === 'admin';
  const [page, setPage] = useState<ReleasePage<FirmwareRelease>>({ items: [], total: 0, offset: 0, limit: 20 });
  const [legacy, setLegacy] = useState<LegacyFirmware[]>([]);
  const [selected, setSelected] = useState<FirmwareRelease | null>(null);
  const [query, setQuery] = useState(''); const [status, setStatus] = useState('');
  const [offset, setOffset] = useState(0); const [loading, setLoading] = useState(false);
  const [busy, setBusy] = useState(''); const [error, setError] = useState(''); const [message, setMessage] = useState('');
  const [notes, setNotes] = useState(''); const [acceptance, setAcceptance] = useState(''); const [reason, setReason] = useState('');
  const [progress, setProgress] = useState(0); const input = useRef<HTMLInputElement>(null);
  const generation = useRef(0);
  const statusName = (s: string) => ({ draft: zh ? '草稿' : 'Draft', published: zh ? '已发布' : 'Published', withdrawn: zh ? '已撤回' : 'Withdrawn' }[s] || s);
  const load = useCallback(async () => {
    if (!isAdmin) return;
    const request = ++generation.current; setLoading(true);
    try {
      const result = await api.list({ query, status, offset });
      if (request === generation.current) setPage(result);
    } catch (e) { if (request === generation.current) setError(e instanceof Error ? e.message : String(e)); }
    finally { if (request === generation.current) setLoading(false); }
  }, [isAdmin, query, status, offset]);
  useEffect(() => { void load(); return () => { generation.current++; }; }, [load]);
  useEffect(() => { if (!isAdmin) { setSelected(null); setPage({ items: [], total: 0, offset: 0, limit: 20 }); setLegacy([]); } }, [isAdmin]);
  function choose(r: FirmwareRelease | null) { setSelected(r); setNotes(r?.notes || ''); setAcceptance(r?.acceptance || ''); setReason(''); }
  async function act(name: string, operation: () => Promise<void>) {
    setBusy(name); setError(''); setMessage('');
    try { await operation(); } catch (e) { setError(e instanceof Error ? e.message : String(e)); }
    finally { setBusy(''); }
  }
  async function upload(file?: File) {
    if (!file) return;
    await act('import', async () => {
      if (file.size > 12 * 1024 * 1024) throw new Error(zh ? '发布包不能超过 12 MiB' : 'Package cannot exceed 12 MiB');
      setProgress(0);
      const result = await api.importBundle(file, setProgress);
      if (result.status !== 'completed' || !result.releaseId) throw new Error(result.error || 'Import failed');
      choose(await api.detail(result.releaseId)); await load();
      setMessage(zh ? '校验完成，已保存为草稿。填写更新说明和验收记录后可发布。' : 'Validated and saved as a draft. Add release notes and acceptance evidence before publishing.');
    });
  }
  async function transition(action: 'publish' | 'withdraw' | 'delete') {
    if (!selected) return;
    const prompt = action === 'publish'
      ? (zh ? `发布 XORA ${selected.manifest.version}？兼容声明和组件将公开显示，产物不能替换。` : `Publish XORA ${selected.manifest.version}? Component details become public and binaries cannot be replaced.`)
      : action === 'withdraw'
        ? (zh ? '撤回后用户将无法浏览此版本。不会撤销已获取的文件。' : 'Withdraw this release from the public catalog? Previously obtained files cannot be revoked.')
        : (zh ? '删除此草稿？审计记录仍会保留。' : 'Delete this draft? Audit records will be retained.');
    if (!window.confirm(prompt)) return;
    await act(action, async () => {
      if (action === 'delete') { await api.remove(selected.id, selected.revision); choose(null); }
      else choose(await (action === 'publish' ? api.publish(selected.id, selected.revision) : api.withdraw(selected.id, selected.revision, reason)));
      await load(); setMessage(zh ? '操作已保存。' : 'Saved.');
    });
  }
  const dirty = selected && (notes !== selected.notes || acceptance !== selected.acceptance);
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
        <Text fontSize="sm" color="fg.muted">{zh ? '签名 ZIP · 最大 12 MiB · 包含 STM32 A/B、TX 和可选 RX。上传不会自动发布。' : 'Signed ZIP · up to 12 MiB · STM32 A/B, TX and optional RX. Uploads are never published automatically.'}</Text>
        <Button colorPalette="green" alignSelf="start" disabled={!!busy} onClick={() => input.current?.click()}>{zh ? '选择发布包' : 'Choose release package'}</Button>
        <input ref={input} hidden type="file" accept=".zip" onChange={e => { const file = e.target.files?.[0]; e.target.value = ''; void upload(file); }} />
        {busy === 'import' && <><Text role="status">{progress < 100 ? `${zh ? '上传中' : 'Uploading'} ${progress}%` : (zh ? '服务器正在验签及检查组件…' : 'Server is verifying signatures and components…')}</Text><Progress.Root value={progress < 100 ? progress : null}><Progress.Track><Progress.Range /></Progress.Track></Progress.Root></>}
      </Stack></AdminCard>
      <AdminCard><Stack gap="5">
      <HStack><LuPackage /><Heading size="lg">{zh ? '发布列表' : 'Releases'}</Heading><Badge variant="subtle">{page.total}</Badge></HStack>
      <Flex gap="3" wrap="wrap"><Input maxW="340px" aria-label={zh ? '搜索版本或说明' : 'Search versions or notes'} placeholder={zh ? '搜索版本或更新说明' : 'Search versions or notes'} value={query} onChange={e => { setQuery(e.target.value); setOffset(0); }} />
        <NativeSelect.Root maxW="200px"><NativeSelect.Field aria-label={zh ? '发布状态' : 'Release status'} value={status} onChange={e => { setStatus(e.target.value); setOffset(0); }}><option value="">{zh ? '全部状态' : 'All states'}</option>{['draft', 'published', 'withdrawn'].map(s => <option key={s} value={s}>{statusName(s)}</option>)}</NativeSelect.Field><NativeSelect.Indicator /></NativeSelect.Root>
        <Button variant="surface" disabled={!!busy} loading={loading} onClick={() => void load()}>{zh ? '刷新列表' : 'Refresh'}</Button></Flex>
      <Stack gap="2">{page.items.map(r => <Flex key={r.id} p="4" borderWidth="1px" borderColor={selected?.id === r.id ? 'green.500' : 'app.border'} borderRadius="lg" bg={selected?.id === r.id ? 'green.subtle' : 'app.canvas'} justify="space-between" gap="3" wrap="wrap">
        <Stack gap="1" minW="0" flex="1"><HStack flexWrap="wrap"><Text fontWeight="bold">XORA {r.manifest.version}</Text><Badge colorPalette={r.status === 'published' ? 'green' : r.status === 'withdrawn' ? 'orange' : 'gray'}>{statusName(r.status)}</Badge></HStack>
          <Text fontSize="sm" color="fg.muted" overflowWrap="anywhere">{zh ? '硬件' : 'Hardware'} {r.manifest.hardwareVersion} · {r.manifest.artifacts.map(a => `${a.component.toUpperCase()}${a.slot || ''} ${a.version}`).join(' / ')}</Text>
          <Text fontSize="xs">{r.publishedAt ? `${zh ? '发布于' : 'Published'} ${new Date(r.publishedAt).toLocaleString()}` : `${zh ? '创建于' : 'Created'} ${new Date(r.createdAt).toLocaleString()}`}</Text>
        </Stack><Button variant="surface" disabled={!!busy} onClick={() => void act('detail', async () => choose(await api.detail(r.id)))}>{zh ? '查看详情' : 'Details'}</Button>
      </Flex>)}{!loading && page.items.length === 0 && <Text color="fg.muted" py="8" textAlign="center">{zh ? '暂无匹配的发布。' : 'No matching releases.'}</Text>}</Stack>
      <HStack justify="space-between" flexWrap="wrap"><Button size="sm" variant="surface" disabled={offset === 0 || loading} onClick={() => setOffset(Math.max(0, offset - 20))}>{zh ? '上一页' : 'Previous'}</Button><Text>{page.total} {zh ? '个版本' : 'releases'}</Text><Button size="sm" variant="surface" disabled={offset + page.limit >= page.total || loading} onClick={() => setOffset(offset + 20)}>{zh ? '下一页' : 'Next'}</Button></HStack>
      </Stack></AdminCard>
      {selected && <AdminCard><Stack gap="4">
        <Flex justify="space-between" wrap="wrap" gap="3"><Heading size="lg">XORA {selected.manifest.version} · {statusName(selected.status)}</Heading><Button size="sm" variant="ghost" onClick={() => choose(null)}>{zh ? '关闭详情' : 'Close'}</Button></Flex>
        <FirmwareReleaseDetails manifest={selected.manifest} zh={zh} />
        <Box><Text fontWeight="semibold">{zh ? '自动校验通过' : 'Automated checks passed'}</Text><Text fontSize="sm">{zh ? '发布签名、文件摘要、STM32 签名包、硬件与无锁构建声明。兼容声明不等于实机验收。' : 'Release signature, file digests, signed STM32 packages, hardware and unlocked build declarations. Compatibility declarations do not replace hardware acceptance.'}</Text></Box>
        <label><Text mb="1">{zh ? '更新说明（用户可见）' : 'Release notes (public)'}</Text><Textarea rows={4} maxLength={10000} readOnly={selected.status !== 'draft' || !!busy} value={notes} onChange={e => setNotes(e.target.value)} /></label>
        <label><Text mb="1">{zh ? '验收记录（仅管理员可见）' : 'Acceptance evidence (administrators only)'}</Text><Textarea rows={3} maxLength={4000} readOnly={selected.status !== 'draft' || !!busy} value={acceptance} placeholder={zh ? '记录测试组件组合、结果与证据位置。' : 'Record tested component versions, results and evidence location.'} onChange={e => setAcceptance(e.target.value)} /></label>
        {selected.reason && <Text>{zh ? '撤回原因：' : 'Withdrawal reason: '}{selected.reason}</Text>}
        {selected.status === 'published' && <label><Text>{zh ? '撤回原因' : 'Withdrawal reason'}</Text><Input maxLength={1000} value={reason} onChange={e => setReason(e.target.value)} /></label>}
        <HStack wrap="wrap">
          {selected.status === 'draft' && <><Button disabled={!!busy || !dirty} onClick={() => void act('save', async () => { choose(await api.edit(selected.id, selected.revision, notes, acceptance)); await load(); setMessage(zh ? '草稿已保存。' : 'Draft saved.'); })}>{zh ? '保存草稿' : 'Save draft'}</Button><Button variant="outline" disabled={!!busy} onClick={() => void transition('delete')}>{zh ? '删除草稿' : 'Delete draft'}</Button></>}
          {selected.status !== 'published' && <Button colorPalette="green" disabled={!!busy || !!dirty || !selected.notes.trim() || !selected.acceptance.trim()} onClick={() => void transition('publish')}>{selected.status === 'withdrawn' ? (zh ? '重新校验并恢复发布' : 'Revalidate & restore') : (zh ? '发布正式版' : 'Publish release')}</Button>}
          {selected.status === 'published' && <Button colorPalette="orange" disabled={!!busy || !reason.trim()} onClick={() => void transition('withdraw')}>{zh ? '撤回发布' : 'Withdraw'}</Button>}
          <Button variant="surface" disabled={!!busy} onClick={() => void act('detail', async () => choose(await api.detail(selected.id)))}>{zh ? '重新加载详情' : 'Reload details'}</Button>
        </HStack>
        <Box><Text fontWeight="semibold">{zh ? '操作记录' : 'Audit trail'}</Text>{selected.audit?.map((event, i) => <Text key={i} fontSize="sm" overflowWrap="anywhere">{new Date(event.at).toLocaleString()} · {event.action} · {event.actor.actorType}/{event.actor.actorId}</Text>)}</Box>
      </Stack></AdminCard>}
      <AdminCard><Button variant="surface" whiteSpace="normal" height="auto" minH="10" py="2" disabled={!!busy} onClick={() => void act('legacy', async () => { const items = await api.legacy(); setLegacy(items); if (!items.length) setMessage(zh ? '没有历史 STM32 发布。' : 'No historical STM32 releases.'); })}>{zh ? '查看历史 STM32 发布（只读）' : 'Historical STM32 releases (read only)'}</Button>
        {legacy.map(r => <Text key={r.id} mt="2">STM32 {r.version} · {r.hardwareVersion} · {r.notes}</Text>)}</AdminCard>
    </>}
  </>;
}
