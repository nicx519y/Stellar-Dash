'use client';

import React, { useCallback, useEffect, useLayoutEffect, useMemo, useRef, useState } from 'react';
import { Box, Button, Center, Flex, HStack, Image, Input, SimpleGrid, Spinner, Text } from '@chakra-ui/react';
import { LuGripVertical, LuTrash2, LuUpload } from 'react-icons/lu';
import { useGamepadConfig } from '@/contexts/gamepad-config-context';
import { galleryImageLimits } from '@/lib/gallery-image-limits';
import { galleryErrorMessage } from '@/lib/gallery-error-message';
import { useLanguage } from '@/contexts/language-context';
import { useUserAuth } from '@/contexts/user-auth-context';
import { AdminCard, AdminPageHeader } from '@/components/admin/admin-surface';
import { Alert } from '@/components/ui/alert';
import { PopoverBody, PopoverContent, PopoverFooter, PopoverHeader, PopoverRoot, PopoverTrigger } from '@/components/ui/popover';
import { processGalleryImage, type GalleryProcessedImage } from '@/lib/gallery-image-processor';
import { galleryDraftChanged, moveGalleryItemToIndex, nearestGallerySlot, planGalleryPublish } from '@/lib/admin/gallery-draft';
import type { GalleryImage } from '@/lib/image-gallery';
import { mapWithConcurrency } from '@/lib/map-with-concurrency';

type Envelope<T> = { success?: boolean; data?: T; error?: string; message?: string };
type DraftItem = {
  id: string;
  title: string;
  previewUrl: string;
} & ({ kind: 'existing'; image: GalleryImage } | { kind: 'new'; file: File; processed: GalleryProcessedImage });
type ItemDrag = { id: string; x: number; y: number; width: number; height: number; previewIndex: number; settling: boolean };

async function jsonRequest<T>(url: string, init?: RequestInit): Promise<T> {
  const response = await fetch(url, { ...init, credentials: 'same-origin', cache: 'no-store', headers: { ...(init?.body ? { 'Content-Type': 'application/json' } : {}), ...init?.headers } });
  const body = await response.json() as Envelope<T>;
  if (!response.ok || body.success !== true || body.data === undefined) throw new Error(body.message || body.error || `HTTP ${response.status}`);
  return body.data;
}

async function fetchOfficialImages(): Promise<GalleryImage[]> {
  const all: GalleryImage[] = [];
  let cursor: string | null = null;
  do {
    const query = cursor ? `&cursor=${encodeURIComponent(cursor)}` : '';
    const result: { items: GalleryImage[]; nextCursor: string | null } = await jsonRequest(`/api/admin/gallery/system?limit=100${query}`);
    all.push(...result.items);
    cursor = result.nextCursor;
  } while (cursor);
  return all;
}

function existingItem(image: GalleryImage): DraftItem {
  return { kind: 'existing', id: image.id, title: image.title,
    previewUrl: image.sourceMime === 'image/gif' ? image.sourceUrl : image.previewUrl, image };
}

async function uploadOfficial(item: Extract<DraftItem, { kind: 'new' }>, sortOrder: number): Promise<void> {
  const form = new FormData();
  form.append('source', item.file, item.file.name);
  form.append('preview', item.processed.preview, 'preview.png');
  form.append('deviceAsset', new Blob([item.processed.deviceAsset]), 'device.uimg');
  form.append('manifest', JSON.stringify({
    title: item.title.trim(), published: true, sortOrder,
    width: item.processed.width, height: item.processed.height,
    frameCount: item.processed.frameCount, fps: item.processed.fps,
    payloadCrc32: item.processed.payloadCrc32,
  }));
  const response = await fetch('/api/admin/gallery/system', { method: 'POST', credentials: 'same-origin', body: form });
  const body = await response.json() as Envelope<GalleryImage>;
  if (!response.ok || body.success !== true || !body.data) throw new Error(body.message || body.error || `HTTP ${response.status}`);
}

export default function AdminImagesPage() {
  const { getDeviceImageCatalog } = useGamepadConfig();
  const { currentLanguage } = useLanguage();
  const { session, loading: sessionLoading } = useUserAuth();
  const zh = currentLanguage === 'zh';
  const [baseline, setBaseline] = useState<GalleryImage[]>([]);
  const [items, setItems] = useState<DraftItem[]>([]);
  const [confirmDeleteId, setConfirmDeleteId] = useState<string | null>(null);
  const [loading, setLoading] = useState(false);
  const [busy, setBusy] = useState<'processing' | 'publishing' | null>(null);
  const [dragActive, setDragActive] = useState(false);
  const [itemDrag, setItemDrag] = useState<ItemDrag | null>(null);
  const [listMinHeight, setListMinHeight] = useState(220);
  const [error, setError] = useState('');
  const input = useRef<HTMLInputElement>(null);
  const dropZone = useRef<HTMLDivElement>(null);
  const dragDepth = useRef(0);
  const beforeItemRects = useRef<Map<string, DOMRect> | null>(null);
  const pointerCleanup = useRef<(() => void) | null>(null);
  const settleTimer = useRef<ReturnType<typeof setTimeout> | null>(null);
  const pendingPreviewUrls = useRef(new Set<string>());
  const isAdmin = session.authenticated && session.user?.role === 'admin';

  useEffect(() => () => {
    pendingPreviewUrls.current.forEach(url => URL.revokeObjectURL(url));
    pendingPreviewUrls.current.clear();
    pointerCleanup.current?.();
    if (settleTimer.current) clearTimeout(settleTimer.current);
  }, []);

  const revokePendingPreviews = useCallback(() => {
    pendingPreviewUrls.current.forEach(url => URL.revokeObjectURL(url));
    pendingPreviewUrls.current.clear();
  }, []);

  const load = useCallback(async () => {
    if (!isAdmin) return;
    setLoading(true);
    try {
      const all = await fetchOfficialImages();
      setBaseline(all);
      setItems(all.map(existingItem));
      setConfirmDeleteId(null);
      setError('');
    } finally { setLoading(false); }
  }, [isAdmin]);
  useEffect(() => {
    void load().catch(reason => setError(reason instanceof Error ? reason.message : String(reason)));
  }, [load]);

  const plan = useMemo(() => planGalleryPublish(baseline, items), [baseline, items]);
  const changed = galleryDraftChanged(plan);
  const dragId = itemDrag?.id;
  const dragPreviewIndex = itemDrag?.previewIndex;
  const previewItems = useMemo(() => dragId !== undefined && dragPreviewIndex !== undefined
    ? moveGalleryItemToIndex(items, dragId, dragPreviewIndex)
    : items, [items, dragId, dragPreviewIndex]);
  const validNames = items.every(item => item.title.trim().length > 0 && item.title.trim().length <= 120);
  const controlsBusy = loading || busy !== null;

  useLayoutEffect(() => {
    const previous = beforeItemRects.current;
    if (!previous || !dropZone.current) return;
    beforeItemRects.current = null;
    dropZone.current.querySelectorAll<HTMLElement>('[data-gallery-item-id]').forEach(node => {
      const id = node.dataset.galleryItemId;
      const from = id ? previous.get(id) : null;
      if (!from) return;
      node.getAnimations().forEach(animation => animation.cancel());
      const to = node.getBoundingClientRect();
      const dx = from.left - to.left;
      const dy = from.top - to.top;
      if (Math.abs(dx) < 1 && Math.abs(dy) < 1) return;
      node.animate([{ transform: `translate3d(${dx}px, ${dy}px, 0)` }, { transform: 'translate3d(0, 0, 0)' }],
        { duration: 190, easing: 'cubic-bezier(0.2, 0.8, 0.2, 1)' });
    });
  }, [previewItems]);

  useLayoutEffect(() => {
    if (!isAdmin) return;
    const measure = () => {
      if (!dropZone.current) return;
      const top = dropZone.current.getBoundingClientRect().top + window.scrollY;
      const remaining = Math.max(220, Math.floor(window.innerHeight - top - 48));
      setListMinHeight(current => current === remaining ? current : remaining);
    };
    measure();
    window.addEventListener('resize', measure);
    return () => window.removeEventListener('resize', measure);
  }, [isAdmin, loading, error, currentLanguage, items.length]);

  const add = async (files: File[]) => {
    if (!files.length || controlsBusy) return;
    setBusy('processing');
    setError('');
    try {
      const limits = galleryImageLimits(await getDeviceImageCatalog());
      const staged: (DraftItem | undefined)[] = new Array(files.length);
      const failures: string[] = [];
      await mapWithConcurrency(files, 4, async (file, index) => {
        try {
          const processed = await processGalleryImage(file, limits);
          const gif = file.type === 'image/gif' || file.name.toLowerCase().endsWith('.gif');
          const previewUrl = URL.createObjectURL(gif ? file : processed.preview);
          pendingPreviewUrls.current.add(previewUrl);
          staged[index] = {
            kind: 'new', id: crypto.randomUUID(), file, processed, previewUrl,
            title: file.name.replace(/\.[^.]+$/, '').trim().slice(0, 120) || 'Image',
          };
        } catch (reason) {
          failures.push(`${file.name}: ${galleryErrorMessage(reason, currentLanguage)}`);
        }
      });
      const ready = staged.filter((item): item is DraftItem => item !== undefined);
      if (ready.length) setItems(current => [...current, ...ready]);
      if (failures.length) setError(failures.join('\n'));
    } catch (error) { setError(galleryErrorMessage(error, currentLanguage)); } finally { setBusy(null); }
  };

  const fileDrag = (event: React.DragEvent<HTMLElement>) => Array.from(event.dataTransfer.types).includes('Files');
  const onDragEnter = (event: React.DragEvent<HTMLDivElement>) => {
    if (!fileDrag(event) && dragDepth.current === 0) return;
    event.preventDefault();
    dragDepth.current += 1;
    if (!controlsBusy) setDragActive(true);
  };
  const onDragOver = (event: React.DragEvent<HTMLDivElement>) => {
    if (!fileDrag(event) && dragDepth.current === 0) return;
    event.preventDefault();
    event.dataTransfer.dropEffect = controlsBusy ? 'none' : 'copy';
  };
  const onDragLeave = (event: React.DragEvent<HTMLDivElement>) => {
    if (dragDepth.current === 0) return;
    event.preventDefault();
    dragDepth.current = Math.max(0, dragDepth.current - 1);
    if (dragDepth.current === 0) setDragActive(false);
  };
  const onDrop = (event: React.DragEvent<HTMLDivElement>) => {
    if (!fileDrag(event) && event.dataTransfer.files.length === 0 && dragDepth.current === 0) return;
    event.preventDefault();
    dragDepth.current = 0;
    setDragActive(false);
    if (!controlsBusy) void add(Array.from(event.dataTransfer.files));
  };

  const captureItemRects = () => {
    const rects = new Map<string, DOMRect>();
    dropZone.current?.querySelectorAll<HTMLElement>('[data-gallery-item-id]').forEach(node => {
      if (node.dataset.galleryItemId) rects.set(node.dataset.galleryItemId, node.getBoundingClientRect());
    });
    beforeItemRects.current = rects;
  };

  const startItemPointerDrag = (event: React.PointerEvent<HTMLDivElement>, item: DraftItem) => {
    if (controlsBusy || itemDrag || event.button !== 0 || event.pointerType === 'touch' ||
      (event.target as HTMLElement).closest('input, button')) return;
    const source = event.currentTarget.getBoundingClientRect();
    const slots = Array.from(dropZone.current?.querySelectorAll<HTMLElement>('[data-gallery-item-id]') || [], node => {
      const rect = node.getBoundingClientRect();
      return { x: rect.left + rect.width / 2, y: rect.top + rect.height / 2 };
    });
    const sourceIndex = items.findIndex(value => value.id === item.id);
    if (sourceIndex < 0 || slots.length !== items.length) return;
    const pointerId = event.pointerId;
    const startX = event.clientX;
    const startY = event.clientY;
    const startScrollY = window.scrollY;
    const previousUserSelect = document.body.style.userSelect;
    let started = false;
    let previewIndex = sourceIndex;

    const cleanup = () => {
      window.removeEventListener('pointermove', move);
      window.removeEventListener('pointerup', finish);
      window.removeEventListener('pointercancel', cancel);
      window.removeEventListener('blur', cancel);
      document.body.style.userSelect = previousUserSelect;
      pointerCleanup.current = null;
    };
    const cancel = () => {
      cleanup();
      if (started) {
        captureItemRects();
        setItemDrag(null);
      }
    };
    const move = (pointer: PointerEvent) => {
      if (pointer.pointerId !== pointerId) return;
      const dx = pointer.clientX - startX;
      const dy = pointer.clientY - startY;
      if (!started && Math.hypot(dx, dy) < 6) return;
      pointer.preventDefault();
      if (!started) { started = true; document.body.style.userSelect = 'none'; }
      const x = source.left + dx;
      const y = source.top + dy;
      const scrollDelta = window.scrollY - startScrollY;
      const index = nearestGallerySlot({ x: x + source.width / 2, y: y + source.height / 2 },
        slots.map(slot => ({ x: slot.x, y: slot.y - scrollDelta })));
      if (index !== previewIndex) captureItemRects();
      previewIndex = index;
      setItemDrag({ id: item.id, x, y, width: source.width, height: source.height, previewIndex, settling: false });
    };
    const finish = (pointer: PointerEvent) => {
      if (pointer.pointerId !== pointerId) return;
      cleanup();
      if (!started) return;
      const placeholder = Array.from(dropZone.current?.querySelectorAll<HTMLElement>('[data-gallery-item-id]') || [])
        .find(node => node.dataset.galleryItemId === item.id);
      const target = placeholder?.getBoundingClientRect() || source;
      setItemDrag(current => current?.id === item.id
        ? { ...current, x: target.left, y: target.top, previewIndex, settling: true }
        : current);
      settleTimer.current = setTimeout(() => {
        setItems(current => moveGalleryItemToIndex(current, item.id, previewIndex));
        setItemDrag(null);
        settleTimer.current = null;
      }, 210);
    };
    pointerCleanup.current = cleanup;
    window.addEventListener('pointermove', move);
    window.addEventListener('pointerup', finish);
    window.addEventListener('pointercancel', cancel);
    window.addEventListener('blur', cancel);
  };

  const removeItem = (item: DraftItem) => {
    if (controlsBusy) return;
    if (item.kind === 'new') {
      URL.revokeObjectURL(item.previewUrl);
      pendingPreviewUrls.current.delete(item.previewUrl);
    }
    setItems(current => current.filter(value => value.id !== item.id));
    setConfirmDeleteId(null);
  };

  const revert = () => {
    if (!changed || controlsBusy) return;
    revokePendingPreviews();
    setItems(baseline.map(existingItem));
    setConfirmDeleteId(null);
    pointerCleanup.current?.();
    setItemDrag(null);
    setError('');
  };

  const publish = async () => {
    if (!changed || !validNames || controlsBusy) return;
    setBusy('publishing');
    setError('');
    try {
      const writeOrder = plan.orderChanged || plan.additions.length > 0;
      const renamed = new Set(plan.renames.map(item => item.id));
      for (const [index, item] of items.entries()) {
        if (item.kind === 'new') await uploadOfficial(item, index);
      }
      for (const [index, item] of items.entries()) {
        if (item.kind !== 'existing' || (!writeOrder && !renamed.has(item.id))) continue;
        await jsonRequest(`/api/admin/gallery/system/${encodeURIComponent(item.id)}`, {
          method: 'PATCH', body: JSON.stringify({
            ...(renamed.has(item.id) ? { title: item.title.trim() } : {}),
            ...(writeOrder ? { sortOrder: index } : {}),
          }),
        });
      }
      for (const id of plan.deletions) {
        await jsonRequest(`/api/admin/gallery/system/${encodeURIComponent(id)}`, { method: 'DELETE' });
      }
      await load();
      revokePendingPreviews();
    } catch (reason) {
      // The existing API commits one item at a time. Re-read after any failure
      // so the page never presents a partially published batch as still local.
      try { await load(); revokePendingPreviews(); } catch { /* Keep the draft visible if the server is unavailable. */ }
      const message = reason instanceof Error ? reason.message : String(reason);
      setError(zh
        ? `发布未全部完成，部分图片可能已更新。已尝试重新读取服务器状态：${message}`
        : `Publication did not finish. Some images may have changed. The server state was reloaded where possible: ${message}`);
    } finally { setBusy(null); }
  };

  return <>
    <AdminPageHeader title={zh ? '官方图库' : 'Official gallery'} description={zh ? '管理 WebConfig 中展示的系统图片。' : 'Manage system images shown in WebConfig.'} />
    {sessionLoading ? <Center minH="320px"><Spinner /></Center> : !session.authenticated ? <Alert colorPalette="orange" title={zh ? '请先登录' : 'Sign in required'} /> : !isAdmin ? <Alert colorPalette="red" title={zh ? '需要管理员权限' : 'Administrator permission required'} /> : <>
      <AdminCard>
        <Flex justify="space-between" align="center" wrap="wrap" gap="3">
          <HStack gap="2">
            <Button variant="surface" disabled={controlsBusy || itemDrag !== null} loading={busy === 'processing'} onClick={() => input.current?.click()}><LuUpload />{zh ? '新增图片' : 'Add images'}</Button>
            <input hidden ref={input} type="file" multiple accept="image/png,image/jpeg,image/gif" onChange={event => { const files = [...(event.target.files || [])]; event.target.value = ''; void add(files); }} />
          </HStack>
          <HStack gap="2">
            <Button variant="surface" disabled={controlsBusy || itemDrag !== null || !changed} onClick={revert}>{zh ? '撤销' : 'Revert'}</Button>
            <Button colorPalette="green" disabled={controlsBusy || itemDrag !== null || !changed || !validNames} loading={busy === 'publishing'} onClick={() => void publish()}>{zh ? '发布' : 'Publish'}</Button>
          </HStack>
        </Flex>
      </AdminCard>
      {error && <Alert colorPalette="red" title={error} />}
      <Box ref={dropZone} position="relative" minH={`${listMinHeight}px`} data-testid="gallery-drop-zone"
        onDragEnter={onDragEnter} onDragOver={onDragOver} onDragLeave={onDragLeave} onDrop={onDrop}>
        {loading && !items.length ? <Center minH={`${listMinHeight}px`}><Spinner /></Center> : items.length === 0 ?
          <Center minH={`${listMinHeight}px`} color="fg.muted"><Text>{zh ? '将 PNG、JPG 或 GIF 图片拖到这里，或点击“新增图片”' : 'Drop PNG, JPG or GIF images here, or click Add images'}</Text></Center> :
        <SimpleGrid templateColumns="repeat(auto-fill, minmax(min(100%, 220px), 1fr))" gap="4">
          {previewItems.map(item => <AdminCard key={item.id} p="0" overflow="hidden" className="group" position="relative"
            data-gallery-item-id={item.id} cursor={controlsBusy ? undefined : itemDrag ? 'grabbing' : 'grab'}
            borderStyle={itemDrag?.id === item.id ? 'dashed' : 'solid'} borderColor={itemDrag?.id === item.id ? 'green.400' : undefined}
            onPointerDown={event => startItemPointerDrag(event, item)}>
            <Box position="relative" width="100%" aspectRatio={320 / 172} bg="gray.900" visibility={itemDrag?.id === item.id ? 'hidden' : 'visible'}>
              <Image src={item.previewUrl} alt={item.title} width="100%" height="100%" objectFit="cover" display="block" draggable={false} />
              <Box position="absolute" top="2" left="2" zIndex="1" p="1" borderRadius="md" bg="blackAlpha.700" color="white" opacity="0.7" pointerEvents="none" aria-hidden="true"><LuGripVertical /></Box>
              <PopoverRoot open={confirmDeleteId === item.id} onOpenChange={details => setConfirmDeleteId(details.open ? item.id : null)}>
                <PopoverTrigger asChild>
                  <Button position="absolute" top="2" right="2" zIndex="1" size="sm" minW="32px" width="32px" height="32px" p="0"
                    bg="blackAlpha.800" color="white" _hover={{ bg: 'red.700' }}
                    opacity={{ base: 1, md: confirmDeleteId === item.id ? 1 : 0 }} _groupHover={{ opacity: 1 }} _focusVisible={{ opacity: 1 }}
                    disabled={controlsBusy || itemDrag !== null} aria-label={zh ? `删除 ${item.title}` : `Delete ${item.title}`}>
                    <LuTrash2 />
                  </Button>
                </PopoverTrigger>
                <PopoverContent width="min(90vw, 300px)">
                  <PopoverHeader fontWeight="semibold">{zh ? '确认删除图片？' : 'Delete this image?'}</PopoverHeader>
                  <PopoverBody><Text fontSize="sm">{zh ? `确定从列表中移除“${item.title}”吗？` : `Remove “${item.title}” from the list?`}</Text>
                    {item.kind === 'existing' && <Text fontSize="xs" color="fg.muted" mt="2">{zh ? '点击“发布”后才会从服务器删除。' : 'It will be deleted from the server when you publish.'}</Text>}
                  </PopoverBody>
                  <PopoverFooter justifyContent="flex-end" gap="2">
                    <Button size="sm" variant="ghost" onClick={() => setConfirmDeleteId(null)}>{zh ? '取消' : 'Cancel'}</Button>
                    <Button size="sm" colorPalette="red" onClick={() => removeItem(item)}>{zh ? '删除' : 'Delete'}</Button>
                  </PopoverFooter>
                </PopoverContent>
              </PopoverRoot>
            </Box>
            <Box p="3" visibility={itemDrag?.id === item.id ? 'hidden' : 'visible'}>
              <Input value={item.title} maxLength={120} disabled={controlsBusy}
                placeholder={zh ? '名称' : 'Name'} aria-label={zh ? '图片名称' : 'Image name'}
                onChange={event => setItems(current => current.map(value => value.id === item.id ? { ...value, title: event.target.value } : value))} />
            </Box>
          </AdminCard>)}
        </SimpleGrid>}
        {dragActive && <Center position="absolute" inset="0" zIndex="3" pointerEvents="none"
          bg="blackAlpha.800" borderWidth="2px" borderStyle="dashed" borderColor="green.500" borderRadius="xl">
          <Text fontSize="lg" fontWeight="semibold" color="white">{zh ? '松开以上传图片' : 'Drop to add images'}</Text>
        </Center>}
        {itemDrag && <Box position="fixed" top="0" left="0" width={`${itemDrag.width}px`} height={`${itemDrag.height}px`}
          zIndex="max" pointerEvents="none" overflow="hidden" borderWidth="1px" borderColor="green.400" borderRadius="xl"
          bg="app.panel" shadow="2xl" opacity="0.96"
          style={{ transform: `translate3d(${itemDrag.x}px, ${itemDrag.y}px, 0)`,
            transition: itemDrag.settling ? 'transform 190ms cubic-bezier(0.2, 0.8, 0.2, 1)' : 'none' }}>
          <Image src={items.find(value => value.id === itemDrag.id)?.previewUrl} alt="" width="100%" aspectRatio={320 / 172} objectFit="cover" draggable={false} />
          <Box p="3"><Text truncate>{items.find(value => value.id === itemDrag.id)?.title}</Text></Box>
        </Box>}
      </Box>
    </>}
  </>;
}
