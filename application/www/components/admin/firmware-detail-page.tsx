'use client';

import { useEffect, useLayoutEffect, useRef, useState } from 'react';
import type { DragEvent } from 'react';
import { useRouter, useSearchParams } from 'next/navigation';
import Link from 'next/link';
import { Badge, Box, Button, Center, Flex, Heading, HStack, Input, Spinner, Stack, Text, Textarea } from '@chakra-ui/react';
import { LuArrowLeft, LuCircleCheck, LuTrash2, LuUpload } from 'react-icons/lu';
import { adminRuntime } from '@hbox/admin-runtime';
import { AdminCard, AdminPageHeader } from '@/components/admin/admin-surface';
import { AdminConfirmDialog } from '@/components/admin/admin-confirm-dialog';
import { FirmwareReleaseDetails } from '@/components/firmware-release-details';
import { Alert } from '@/components/ui/alert';
import { useLanguage } from '@/contexts/language-context';
import { useUserAuth } from '@/contexts/user-auth-context';
import type { FirmwareRelease } from '@/lib/admin/firmware-types';
import { MarkdownImportError, NotesEditor, readMarkdownNotes, type NotesEditorState } from '@/lib/admin/firmware-notes-editor';

const api = adminRuntime.firmware;
type ReleaseAction = 'publish' | 'withdraw' | 'delete';

export default function AdminFirmwareDetailPage() {
  const router = useRouter();
  const id = useSearchParams().get('id')?.trim() || '';
  const { currentLanguage } = useLanguage(); const zh = currentLanguage === 'zh';
  const { session, loading: authLoading } = useUserAuth();
  const isAdmin = session.authenticated && session.user?.role === 'admin';
  const [release, setRelease] = useState<FirmwareRelease | null>(null);
  const [loading, setLoading] = useState(false);
  const [busy, setBusy] = useState(''); const [error, setError] = useState('');
  const [reason, setReason] = useState('');
  const [pendingAction, setPendingAction] = useState<ReleaseAction | null>(null);
  const transitioning = useRef(false);
  const [draggingNotes, setDraggingNotes] = useState(false);
  const [saveState, setSaveState] = useState<NotesEditorState>({ notes: '', savedNotes: '', revision: 0, status: 'saved', error: '' });
  const editor = useRef<NotesEditor<FirmwareRelease> | null>(null);
  const mdInput = useRef<HTMLInputElement>(null);
  const notesInput = useRef<HTMLTextAreaElement>(null);
  const statusName = (s: string) => ({ draft: zh ? '草稿' : 'Draft', published: zh ? '已发布' : 'Published', withdrawn: zh ? '已撤回' : 'Withdrawn' }[s] || s);

  function show(value: FirmwareRelease) {
    editor.current?.dispose();
    setRelease(value); setReason(''); setDraggingNotes(false); setPendingAction(null);
    editor.current = value.status === 'draft' ? new NotesEditor<FirmwareRelease>({
      notes: value.notes, revision: value.revision,
      save: (revision, notes) => api.edit(value.id, revision, notes, value.acceptance ?? ''),
      onState: setSaveState,
      onSaved: saved => setRelease(saved),
    }) : null;
    if (!editor.current) setSaveState({ notes: value.notes, savedNotes: value.notes, revision: value.revision, status: 'saved', error: '' });
  }
  useEffect(() => {
    if (!isAdmin || !id) { editor.current?.dispose(); editor.current = null; setRelease(null); return; }
    let cancelled = false;
    setRelease(null); setError(''); setLoading(true);
    void api.detail(id).then(value => { if (!cancelled) show(value); })
      .catch(cause => { if (!cancelled) setError(cause instanceof Error ? cause.message : String(cause)); })
      .finally(() => { if (!cancelled) setLoading(false); });
    return () => { cancelled = true; editor.current?.dispose(); editor.current = null; };
  }, [isAdmin, id]);
  useEffect(() => {
    const warn = (event: BeforeUnloadEvent) => {
      if (!editor.current?.hasPendingChanges) return;
      event.preventDefault(); event.returnValue = '';
    };
    window.addEventListener('beforeunload', warn);
    return () => window.removeEventListener('beforeunload', warn);
  }, []);
  useLayoutEffect(() => {
    const input = notesInput.current;
    if (!input) return;
    const resize = () => {
      input.style.height = 'auto';
      input.style.height = `${input.scrollHeight + input.offsetHeight - input.clientHeight}px`;
    };
    resize();
    const container = input.parentElement;
    if (!container) return;
    let width = container.clientWidth;
    const observer = new ResizeObserver(() => {
      if (container.clientWidth === width) return;
      width = container.clientWidth;
      resize();
    });
    observer.observe(container);
    return () => observer.disconnect();
  }, [saveState.notes, release?.status]);
  async function importMarkdown(file?: File) {
    if (!file || !editor.current) return;
    const current = editor.current;
    setError('');
    try {
      const notes = await readMarkdownNotes(file);
      if (editor.current === current) current.change(notes, true);
    } catch (cause) {
      const code = cause instanceof MarkdownImportError ? cause.code : 'encoding';
      const labels = zh
        ? { type: '请选择 .md 文件', size: 'Markdown 文件不能超过 64 KiB', encoding: '文件必须是有效的 UTF-8 文本', empty: '更新说明不能为空', length: '更新说明不能超过 10,000 字符' }
        : { type: 'Choose a .md file', size: 'Markdown file cannot exceed 64 KiB', encoding: 'File must contain valid UTF-8 text', empty: 'Release notes cannot be empty', length: 'Release notes cannot exceed 10,000 characters' };
      setError(labels[code]);
    }
  }
  function onNotesDragEnter(event: DragEvent<HTMLTextAreaElement>) {
    if (!Array.from(event.dataTransfer.types).includes('Files')) return;
    event.preventDefault();
    if (release?.status === 'draft' && !busy) setDraggingNotes(true);
  }
  function onNotesDragOver(event: DragEvent<HTMLTextAreaElement>) {
    if (!Array.from(event.dataTransfer.types).includes('Files')) return;
    event.preventDefault();
    event.dataTransfer.dropEffect = release?.status === 'draft' && !busy ? 'copy' : 'none';
  }
  function onNotesDrop(event: DragEvent<HTMLTextAreaElement>) {
    if (!Array.from(event.dataTransfer.types).includes('Files') && event.dataTransfer.files.length === 0) return;
    event.preventDefault();
    setDraggingNotes(false);
    if (release?.status !== 'draft' || busy) return;
    if (event.dataTransfer.files.length !== 1) {
      setError(zh ? '请只拖入一个 .md 文件' : 'Drop one .md file at a time');
      return;
    }
    void importMarkdown(event.dataTransfer.files[0]);
  }
  async function act(name: string, operation: () => Promise<void>) {
    setBusy(name); setError('');
    try { await operation(); } catch (cause) { setError(cause instanceof Error ? cause.message : String(cause)); }
    finally { setBusy(''); }
  }
  function requestTransition(action: ReleaseAction) {
    if (!release || busy) return;
    if (action === 'publish' && (editor.current?.hasPendingChanges || saveState.status !== 'saved' || !release.notes.trim())) return;
    if (action === 'withdraw' && !reason.trim()) return;
    setPendingAction(action);
  }
  async function confirmTransition() {
    const action = pendingAction;
    if (!action || !release || busy || transitioning.current || !isAdmin) return;
    if (action === 'publish' && (editor.current?.hasPendingChanges || saveState.status !== 'saved' || !release.notes.trim())) {
      setPendingAction(null);
      return;
    }
    setPendingAction(null);
    transitioning.current = true;
    await act(action, async () => {
      if (action === 'delete') {
        if (editor.current?.hasPendingChanges) throw new Error(zh ? '请等待更新说明保存后再删除。' : 'Wait for release notes to finish saving before deleting.');
        await api.remove(release.id, release.revision);
        editor.current?.dispose(); editor.current = null;
        router.replace('/admin/firmware/');
      } else {
        show(await (action === 'publish' ? api.publish(release.id, release.revision) : api.withdraw(release.id, release.revision, reason)));
      }
    });
    transitioning.current = false;
  }
  return <>
    <AdminPageHeader title={release ? `XORA ${release.manifest.version}` : (zh ? '固件详情' : 'Firmware details')}
      description={zh ? '查看版本信息、修改草稿并管理发布状态。' : 'Review the release, edit its draft and manage publication.'}
      actions={<Button asChild variant="surface"><Link href="/admin/firmware/" onClick={event => {
        if (!editor.current?.hasPendingChanges) return;
        event.preventDefault();
        void editor.current.flush().then(saved => { if (saved) router.push('/admin/firmware/'); });
      }}><LuArrowLeft />{zh ? '返回固件列表' : 'Back to releases'}</Link></Button>} />
    {authLoading ? <Center minH="320px"><Spinner size="lg" colorPalette="green" /></Center>
      : !isAdmin ? <Alert colorPalette="orange" title={zh ? '请使用管理员账户登录。' : 'Sign in with an administrator account.'} />
      : !id ? <Alert colorPalette="orange" title={zh ? '缺少固件版本 ID。' : 'Missing firmware release ID.'} /> : <>
        {process.env.NEXT_PUBLIC_OFFLINE_PREVIEW === 'true' && <Box bg="purple.subtle" color="purple.fg" borderRadius="lg" p="3" fontSize="sm">{zh ? 'MOCK 预览：数据仅存于本标签页，不执行真实签名校验或发布。' : 'MOCK PREVIEW: tab-local data, no real signature verification or publishing.'}</Box>}
        {error && <Box role="alert" p="3" borderWidth="1px" borderColor="red.500" overflowWrap="anywhere">{error}</Box>}
        {loading && <Center minH="240px"><Spinner colorPalette="green" /></Center>}
        {release && <AdminCard p={{ base: 5, md: 8 }} color="fg" fontSize="sm" lineHeight="1.7" letterSpacing="normal"><Stack gap={7}>
          <Flex justify="space-between" align="center" wrap="wrap" gap={4}>
            <HStack gap={3}><Heading as="h2" fontSize="xl" fontWeight="semibold" lineHeight="1.4">{zh ? '版本概览' : 'Release overview'}</Heading><Badge colorPalette={release.status === 'published' ? 'green' : release.status === 'withdrawn' ? 'orange' : 'gray'}>{statusName(release.status)}</Badge></HStack>
            <HStack wrap="wrap" justify="flex-end" gap="2">
              {release.status === 'draft' && <>
                <Text role="status" fontSize="xs" color={saveState.status === 'error' ? 'red.500' : 'fg.muted'}>
                  {({ saved: zh ? '已保存' : 'Saved', unsaved: zh ? '未保存' : 'Unsaved', saving: zh ? '保存中…' : 'Saving…', error: zh ? '保存失败' : 'Save failed' })[saveState.status]}
                </Text>
                {saveState.status === 'error' && <Text role="alert" fontSize="xs" color="red.500" maxW="260px" overflowWrap="anywhere">{saveState.error}</Text>}
                {saveState.status === 'error' && <Button size="xs" variant="ghost" onClick={() => void editor.current?.flush()}>{zh ? '重试' : 'Retry'}</Button>}
              </>}
              <Button size="sm" variant="outline" colorPalette="red" loading={busy === 'delete'} disabled={!!busy || saveState.status === 'saving' || saveState.status === 'unsaved'} onClick={() => requestTransition('delete')}><LuTrash2 />{zh ? '删除固件' : 'Delete firmware'}</Button>
              {release.status !== 'published' && <Button size="sm" colorPalette="green" disabled={!!busy || saveState.status !== 'saved' || !release.notes.trim()} onClick={() => requestTransition('publish')}>{release.status === 'withdrawn' ? (zh ? '重新校验并恢复发布' : 'Revalidate & restore') : (zh ? '发布正式版' : 'Publish release')}</Button>}
              {release.status === 'published' && <Button size="sm" colorPalette="orange" disabled={!!busy || !reason.trim()} onClick={() => requestTransition('withdraw')}>{zh ? '撤回发布' : 'Withdraw'}</Button>}
            </HStack>
          </Flex>
          <FirmwareReleaseDetails manifest={release.manifest} zh={zh} />
          <Stack gap={2.5}>
            <HStack gap={2} align="center"><Box color="green.fg" fontSize="lg"><LuCircleCheck /></Box><Heading as="h2" fontSize="md" fontWeight="semibold" lineHeight="1.4">{zh ? '自动校验通过' : 'Automated checks passed'}</Heading></HStack>
            <Text fontSize="sm" color="fg.muted" lineHeight="1.8" maxW="960px">{zh ? '发布签名、文件摘要、STM32 签名包、硬件与无锁构建声明。兼容声明不等于实机验收。' : 'Release signature, file digests, signed STM32 packages, hardware and unlocked build declarations. Compatibility declarations do not replace hardware acceptance.'}</Text>
          </Stack>
          <Box pt={6} borderTopWidth="1px" borderColor="app.border"><Flex align="center" justify="space-between" wrap="wrap" gap={3} mb={4}><Heading as="h2" fontSize="lg" fontWeight="semibold" lineHeight="1.4"><label htmlFor="release-notes">{zh ? '更新说明（用户可见）' : 'Release notes (public)'}</label></Heading>
            {release.status === 'draft' && <><Button size="sm" variant="surface" disabled={!!busy} onClick={() => mdInput.current?.click()}><LuUpload />{zh ? '导入 .md' : 'Import .md'}</Button>
              <input ref={mdInput} hidden type="file" accept=".md,text/markdown" onChange={event => { const file = event.target.files?.[0]; event.target.value = ''; void importMarkdown(file); }} /></>}
          </Flex><Box position="relative"><Textarea ref={notesInput} id="release-notes" rows={4} maxLength={10000} readOnly={release.status !== 'draft' || !!busy} value={saveState.notes}
            resize="none" overflow="hidden" fontSize="sm" lineHeight="1.8" letterSpacing="normal" p={{ base: 4, md: 5 }} bg="app.canvas"
            borderColor={draggingNotes ? 'green.500' : undefined} borderStyle={draggingNotes ? 'dashed' : undefined}
            onChange={event => editor.current?.change(event.target.value)} onBlur={() => { void editor.current?.flush(); }}
            onDragEnter={onNotesDragEnter} onDragOver={onNotesDragOver} onDragLeave={() => setDraggingNotes(false)} onDrop={onNotesDrop} />
            {draggingNotes && <Flex position="absolute" inset="0" pointerEvents="none" align="center" justify="center" borderRadius="md" borderWidth="2px" borderStyle="dashed" borderColor="green.500" bg="green.subtle" color="green.fg" fontWeight="medium">{zh ? '松开以导入 .md 并替换更新说明' : 'Drop .md to replace release notes'}</Flex>}
          </Box></Box>
          {release.reason && <Text fontSize="sm" color="fg.muted" overflowWrap="anywhere">{zh ? '撤回原因：' : 'Withdrawal reason: '}{release.reason}</Text>}
          {release.status === 'published' && <label><Text fontWeight="medium" mb={3}>{zh ? '撤回原因' : 'Withdrawal reason'}</Text><Input maxW="720px" maxLength={1000} value={reason} onChange={e => setReason(e.target.value)} /></label>}
          <Stack gap={4} pt={6} borderTopWidth="1px" borderColor="app.border">
            <Heading as="h2" fontSize="md" fontWeight="medium" color="fg.muted" lineHeight="1.4">{zh ? '操作记录' : 'Audit trail'}</Heading>
            <Stack gap={3}>{release.audit?.map((event, index) => <Flex key={index} direction={{ base: 'column', md: 'row' }} align={{ base: 'flex-start', md: 'center' }} gap={{ base: 1.5, md: 3 }} fontSize="xs" color="fg.muted">
              <Text flexShrink={0}>{new Date(event.at).toLocaleString()}</Text>
              <Badge variant="subtle" fontWeight="medium">{event.action}</Badge>
              <Text fontFamily="mono" overflowWrap="anywhere" minW="0">{event.actor.actorType}/{event.actor.actorId}</Text>
            </Flex>)}</Stack>
          </Stack>
        </Stack></AdminCard>}
      </>}
    <AdminConfirmDialog open={pendingAction !== null} onClose={() => setPendingAction(null)} onConfirm={() => void confirmTransition()}
      title={pendingAction === 'publish' ? (zh ? '确认发布正式版' : 'Confirm publication')
        : pendingAction === 'withdraw' ? (zh ? '确认撤回发布' : 'Confirm withdrawal') : (zh ? '确认删除固件' : 'Confirm firmware deletion')}
      description={<Text>{pendingAction === 'publish'
        ? (zh ? `发布 XORA ${release?.manifest.version}？兼容声明和组件将公开显示，产物不能替换。` : `Publish XORA ${release?.manifest.version}? Component details become public and binaries cannot be replaced.`)
        : pendingAction === 'withdraw'
          ? (zh ? '撤回后用户将无法浏览此版本。不会撤销已获取的文件。' : 'Withdraw this release from the public catalog? Previously obtained files cannot be revoked.')
          : (zh ? `删除 XORA ${release?.manifest.version}？删除后将从管理列表和公开目录移除，无法再下载。此操作不可恢复；审计记录和包文件仍会保留，已下载或安装的固件不受影响。` : `Delete XORA ${release?.manifest.version}? It will be removed from the admin list and public catalog and will no longer be downloadable. This cannot be undone. Audit records and package files are retained; existing downloads and installations are unaffected.`)}</Text>}
      cancelLabel={zh ? '取消' : 'Cancel'}
      confirmLabel={pendingAction === 'publish' ? (zh ? '确认发布' : 'Publish release')
        : pendingAction === 'withdraw' ? (zh ? '确认撤回' : 'Withdraw release') : (zh ? '确认删除' : 'Delete firmware')}
      confirmColorPalette={pendingAction === 'publish' ? 'green' : pendingAction === 'withdraw' ? 'orange' : 'red'} />
  </>;
}
