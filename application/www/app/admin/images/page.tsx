'use client';

import React, { useCallback, useEffect, useRef, useState } from 'react';
import { Badge, Box, Button, Center, Container, Flex, Heading, HStack, Image, Input, SimpleGrid, Spinner, Stack, Switch, Text } from '@chakra-ui/react';
import { LuArrowLeft, LuImagePlus, LuRefreshCw, LuSave, LuTrash2, LuUpload } from 'react-icons/lu';
import { useLanguage } from '@/contexts/language-context';
import { useUserAuth } from '@/contexts/user-auth-context';
import { UserAuthControl } from '@/components/user-auth-control';
import { LanguageSwitcher } from '@/components/language-switcher';
import { Alert } from '@/components/ui/alert';
import { processGalleryImage } from '@/lib/gallery-image-processor';
import type { GalleryImage } from '@/lib/image-gallery';
import { mapWithConcurrency } from '@/lib/map-with-concurrency';

type Envelope<T> = { success?: boolean; data?: T; error?: string; message?: string };

async function jsonRequest<T>(url: string, init?: RequestInit): Promise<T> {
  const response = await fetch(url, { ...init, credentials: 'same-origin', cache: 'no-store', headers: { ...(init?.body ? { 'Content-Type': 'application/json' } : {}), ...init?.headers } });
  const body = await response.json() as Envelope<T>;
  if (!response.ok || body.success !== true || body.data === undefined) throw new Error(body.message || body.error || `HTTP ${response.status}`);
  return body.data;
}

async function uploadOfficial(file: File, published: boolean): Promise<void> {
  const processed = await processGalleryImage(file);
  const form = new FormData();
  form.append('source', file, file.name);
  form.append('preview', processed.preview, 'preview.png');
  form.append('deviceAsset', new Blob([processed.deviceAsset]), 'device.uimg');
  form.append('manifest', JSON.stringify({ title: file.name.replace(/\.[^.]+$/, ''), published, width: processed.width, height: processed.height, frameCount: processed.frameCount, fps: processed.fps, payloadCrc32: processed.payloadCrc32 }));
  const response = await fetch('/api/admin/gallery/system', { method: 'POST', credentials: 'same-origin', body: form });
  const body = await response.json() as Envelope<GalleryImage>;
  if (!response.ok || body.success !== true) throw new Error(body.message || body.error || `HTTP ${response.status}`);
}

export default function AdminImagesPage() {
  const { currentLanguage } = useLanguage();
  const { session, loading: sessionLoading } = useUserAuth();
  const zh = currentLanguage === 'zh';
  const [items, setItems] = useState<GalleryImage[]>([]);
  const [loading, setLoading] = useState(false);
  const [busy, setBusy] = useState<string | null>(null);
  const [publishUploads, setPublishUploads] = useState(true);
  const input = useRef<HTMLInputElement>(null);
  const isAdmin = session.authenticated && session.user?.role === 'admin';

  const load = useCallback(async () => {
    if (!isAdmin) return;
    setLoading(true);
    try {
      const all: GalleryImage[] = [];
      let cursor: string | null = null;
      do {
        const query = cursor ? `&cursor=${encodeURIComponent(cursor)}` : '';
        const result: { items: GalleryImage[]; nextCursor: string | null } = await jsonRequest(`/api/admin/gallery/system?limit=100${query}`);
        all.push(...result.items);
        cursor = result.nextCursor;
      } while (cursor);
      setItems(all);
    } finally { setLoading(false); }
  }, [isAdmin]);
  useEffect(() => { void load(); }, [load]);

  const patch = async (item: GalleryImage) => {
    setBusy(item.id);
    try { await jsonRequest(`/api/admin/gallery/system/${encodeURIComponent(item.id)}`, { method: 'PATCH', body: JSON.stringify({ title: item.title, sortOrder: item.sortOrder, published: item.published }) }); await load(); }
    catch (error) { alert(error instanceof Error ? error.message : String(error)); }
    finally { setBusy(null); }
  };
  const remove = async (item: GalleryImage) => {
    if (!confirm(zh ? `删除“${item.title}”？` : `Delete “${item.title}”?`)) return;
    setBusy(item.id);
    try { await jsonRequest(`/api/admin/gallery/system/${encodeURIComponent(item.id)}`, { method: 'DELETE' }); await load(); }
    catch (error) { alert(error instanceof Error ? error.message : String(error)); }
    finally { setBusy(null); }
  };
  const add = async (files: File[]) => {
    setBusy('upload');
    try {
      await mapWithConcurrency(files, 4, file => uploadOfficial(file, publishUploads));
      await load();
    } catch (error) { alert(error instanceof Error ? error.message : String(error)); }
    finally { setBusy(null); }
  };

  return <Box minHeight="100vh" bg="app.canvas"><Container maxWidth="7xl" py={{ base: 5, md: 8 }}><Stack gap="6">
    <Flex justify="space-between" gap="4" wrap="wrap"><Stack gap="1"><HStack><LuImagePlus /><Heading size="2xl">{zh ? '官方图库管理' : 'Official Gallery'}</Heading></HStack><Text color="fg.muted">{zh ? '管理 WebConfig 中展示的系统图片。' : 'Manage system images shown in WebConfig.'}</Text></Stack><HStack>
      <Button variant="surface" onClick={() => { window.location.href = '/admin/users/'; }}><LuArrowLeft />{zh ? '账户管理' : 'Accounts'}</Button><UserAuthControl /><LanguageSwitcher />
    </HStack></Flex>
    {sessionLoading ? <Center minH="320px"><Spinner /></Center> : !session.authenticated ? <Alert colorPalette="orange" title={zh ? '请先登录' : 'Sign in required'} /> : !isAdmin ? <Alert colorPalette="red" title={zh ? '需要管理员权限' : 'Administrator permission required'} /> : <>
      <Flex justify="space-between" wrap="wrap" gap="3"><HStack><Switch.Root checked={publishUploads} onCheckedChange={details => setPublishUploads(details.checked)}><Switch.HiddenInput /><Switch.Control /><Switch.Label>{zh ? '上传后立即发布' : 'Publish uploads'}</Switch.Label></Switch.Root></HStack><HStack><Button variant="surface" loading={loading} onClick={() => void load()}><LuRefreshCw />{zh ? '刷新' : 'Refresh'}</Button><Button colorPalette="green" loading={busy === 'upload'} onClick={() => input.current?.click()}><LuUpload />{zh ? '批量新增' : 'Add images'}</Button><input hidden ref={input} type="file" multiple accept="image/png,image/jpeg,image/gif" onChange={event => { const files = [...(event.target.files || [])]; event.target.value = ''; void add(files); }} /></HStack></Flex>
      <SimpleGrid columns={{ base: 1, md: 2, xl: 3 }} gap="4">{items.map((item) => <Box key={item.id} borderWidth="1px" borderColor="app.border" borderRadius="xl" p="4"><Stack gap="3">
        <Box height="150px" bg="gray.900" borderRadius="md" overflow="hidden"><Image src={item.previewUrl} alt={item.title} width="100%" height="100%" objectFit="contain" /></Box>
        <HStack justify="space-between"><Badge colorPalette={item.published ? 'green' : 'orange'}>{item.published ? (zh ? '已发布' : 'Published') : (zh ? '未发布' : 'Draft')}</Badge><Text fontSize="xs">{item.frameCount} frame(s)</Text></HStack>
        <Input value={item.title} maxLength={120} onChange={event => setItems(current => current.map(value => value.id === item.id ? { ...value, title: event.target.value } : value))} />
        <HStack><Text fontSize="sm">{zh ? '顺序' : 'Order'}</Text><Input type="number" value={item.sortOrder} onChange={event => setItems(current => current.map(value => value.id === item.id ? { ...value, sortOrder: Number(event.target.value) || 0 } : value))} /><Switch.Root checked={item.published} onCheckedChange={details => setItems(current => current.map(value => value.id === item.id ? { ...value, published: details.checked } : value))}><Switch.HiddenInput /><Switch.Control /></Switch.Root></HStack>
        <HStack><Button flex="1" size="sm" loading={busy === item.id} onClick={() => void patch(item)}><LuSave />{zh ? '保存' : 'Save'}</Button><Button size="sm" colorPalette="red" disabled={busy !== null} onClick={() => void remove(item)}><LuTrash2 /></Button></HStack>
      </Stack></Box>)}</SimpleGrid>
    </>}
  </Stack></Container></Box>;
}
