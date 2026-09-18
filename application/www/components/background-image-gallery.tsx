'use client';

import React, { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { Box, Button, Dialog, Flex, Grid, HStack, Image, Spinner, Tabs, Text, VStack } from '@chakra-ui/react';
import { LuCheck, LuImagePlus, LuPlus, LuTrash2 } from 'react-icons/lu';
import { useGamepadConfig } from '@/contexts/gamepad-config-context';
import { useUserAuth } from '@/contexts/user-auth-context';
import { useLanguage } from '@/contexts/language-context';
import { showToast } from './ui/toaster';
import { processGalleryImage } from '@/lib/gallery-image-processor';
import {
  deleteMyGalleryImages,
  fetchGalleryImageMatch,
  fetchMyGallery,
  fetchSystemGallery,
  GalleryImage,
  uploadMyGalleryImage,
} from '@/lib/image-gallery';
import { parseUimgV3, sha256Hex } from '@/lib/uimg-v3';
import { mapWithConcurrency } from '@/lib/map-with-concurrency';
import {
  clearDeviceImagePreview,
  loadDeviceImagePreview,
  saveDeviceImagePreview,
} from '@/lib/device-image-preview-cache';
import { advanceHoldGesture, isShortSelectionPress } from '@/lib/hold-progress';
import {
  beginDeviceImageInstall,
  deviceImageInstallRingProgress,
  finishDeviceImageInstall,
  getDeviceImageInstallState,
  updateDeviceImageInstall,
  useDeviceImageInstallState,
} from '@/lib/device-image-install-lock';
import type { ScreenControlConfig } from '@/types/gamepad-config';

const MAX_SOURCE_BYTES = 20 * 1024 * 1024;
const GALLERY_TAB_STORAGE_KEY = 'hbox-background-gallery-tab-v1';
const DEVICE_SCREEN_WIDTH = 320;
const DEVICE_SCREEN_HEIGHT = 172;

type GalleryTab = 'system' | 'mine';

type UploadState = {
  id: string;
  file: File;
  previewUrl: string;
  status: 'queued' | 'processing' | 'uploading' | 'error';
  progress: number;
  error?: string;
};

type Props = {
  disabled: boolean;
  config: ScreenControlConfig;
  onInstalled(): void;
  onAvailabilityChange(available: boolean): void;
  onBusyChange(busy: boolean): void;
};

function fingerprint(value: { width: number; height: number; size: number; frameCount: number; fps: number; crc32?: number }) {
  return [value.width, value.height, value.size, value.frameCount, value.fps, value.crc32 ?? 'legacy'].join(':');
}

function AuthorizedPreview({ image, fetchAuthorized }: { image: GalleryImage; fetchAuthorized: PropsWithFetch }) {
  const [url, setUrl] = useState(image.scope === 'user' ? image.previewUrl : '');
  useEffect(() => {
    if (image.scope === 'user') { setUrl(image.previewUrl); return; }
    let active = true;
    let objectUrl = '';
    fetchAuthorized(image.previewUrl).then(async response => {
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      objectUrl = URL.createObjectURL(await response.blob());
      if (active) setUrl(objectUrl);
    }).catch(() => { if (active) setUrl(''); });
    return () => { active = false; if (objectUrl) URL.revokeObjectURL(objectUrl); };
  }, [fetchAuthorized, image.previewUrl, image.scope]);
  return url ? <Image src={url} alt={image.title} width="100%" height="100%" objectFit="contain" draggable={false} /> : <Spinner size="sm" />;
}

type PropsWithFetch = (input: RequestInfo | URL, init?: RequestInit) => Promise<Response>;

async function loadGallerySourcePreview(
  image: GalleryImage,
  fetchAuthorized: PropsWithFetch,
): Promise<string> {
  const response = await fetchAuthorized(image.sourceUrl, { cache: 'force-cache' });
  if (!response.ok) throw new Error(`HTTP ${response.status}`);
  const source = await response.blob();
  const mime = (source.type || image.sourceMime).toLowerCase();
  if (source.size < 1 || source.size > MAX_SOURCE_BYTES ||
      !['image/png', 'image/jpeg', 'image/gif'].includes(mime)) {
    throw new Error('Gallery source image is invalid');
  }
  return URL.createObjectURL(source);
}

export function BackgroundImageGallery({ disabled, config, onInstalled, onAvailabilityChange, onBusyChange }: Props) {
  const {
    deviceConnected, dataIsReady, deviceSession, getDeviceImageCatalog,
    uploadDeviceImage, stageDeferredScreenControl, fetchDeviceAuthorizedResource,
  } = useGamepadConfig();
  const { session } = useUserAuth();
  const { currentLanguage } = useLanguage();
  const zh = currentLanguage === 'zh';
  const copy = zh ? {
    system: '系统图片', mine: '我的图片', empty: '设备当前没有安装图片', local: '仅设备本地', installed: '已安装',
    hold: '长按 2 秒安装到设备', add: '批量新增', remove: '批量删除', signIn: '登录账户后可使用个人图库',
    failed: '上传失败', retry: '重新上传',
    limitReached: '图片数量已达上限', limitReachedDescription: '要继续增加图片，可以先删除一些不需要的图片。',
    quota: (count: number, limit: number) => `${count}/${limit} 张`, loadMore: '加载更多', uploading: '正在写入设备',
  } : {
    system: 'System Images', mine: 'My Images', empty: 'No image is installed on this device', local: 'Device only', installed: 'Installed',
    hold: 'Hold for 2 seconds to install', add: 'Add images', remove: 'Delete selected', signIn: 'Sign in to use your personal gallery',
    failed: 'Upload failed', retry: 'Upload again',
    limitReached: 'Image limit reached', limitReachedDescription: 'Delete some images you no longer need before adding more.',
    quota: (count: number, limit: number) => `${count}/${limit} images`, loadMore: 'Load more', uploading: 'Installing on device',
  };
  const [open, setOpen] = useState(false);
  const [galleryTab, setGalleryTab] = useState<GalleryTab>('system');
  const [systemImages, setSystemImages] = useState<GalleryImage[]>([]);
  const [systemCursor, setSystemCursor] = useState<string | null>(null);
  const [mine, setMine] = useState<{ limit: number; count: number; items: GalleryImage[] }>({ limit: 10, count: 0, items: [] });
  const [selected, setSelected] = useState<Set<string>>(new Set());
  const [uploads, setUploads] = useState<UploadState[]>([]);
  const [retryHoverId, setRetryHoverId] = useState<string | null>(null);
  const [loading, setLoading] = useState(false);
  const [currentPreview, setCurrentPreview] = useState<string>('');
  const [currentFingerprint, setCurrentFingerprint] = useState<string>('');
  const [currentGalleryId, setCurrentGalleryId] = useState<string | null>(null);
  const [displayPixelRatio, setDisplayPixelRatio] = useState(1);
  const deviceInstall = useDeviceImageInstallState();
  const installingId = deviceInstall?.imageId || null;
  const deviceProgress = deviceInstall?.progress ?? null;
  const [hold, setHold] = useState<{ id: string; progress: number; delayMs: number; pressed: boolean }>({ id: '', progress: 0, delayMs: 0, pressed: false });
  const fileInput = useRef<HTMLInputElement>(null);
  const syncInFlight = useRef<Promise<void> | null>(null);
  const syncEpoch = useRef(0);
  const uploadPreviewUrls = useRef(new Set<string>());
  const holdLast = useRef(0);
  const pressGesture = useRef<{ id: string; startedAt: number; resumed: boolean } | null>(null);
  const installTriggered = useRef(false);
  const configRef = useRef(config);
  const deviceId = deviceSession?.deviceId || null;
  const deviceSessionId = deviceSession?.sessionId || null;
  const accountUid = session.user?.uid || null;
  const previewIdentity = useMemo(
    () => ({ deviceId, sessionId: deviceSessionId }),
    [deviceId, deviceSessionId],
  );

  useEffect(() => { configRef.current = config; }, [config]);
  useEffect(() => {
    const updateDisplayPixelRatio = () => setDisplayPixelRatio(
      Math.max(1, Number(window.devicePixelRatio) || 1),
    );
    updateDisplayPixelRatio();
    window.addEventListener('resize', updateDisplayPixelRatio);
    return () => window.removeEventListener('resize', updateDisplayPixelRatio);
  }, []);
  useEffect(() => {
    try {
      const stored = localStorage.getItem(GALLERY_TAB_STORAGE_KEY);
      if (stored === 'system' || stored === 'mine') setGalleryTab(stored);
    } catch {
      // Storage can be unavailable in privacy modes; the system tab remains a
      // safe deterministic fallback.
    }
  }, []);
  useEffect(() => { setSelected(new Set()); }, [session.user?.uid]);
  useEffect(() => () => {
    uploadPreviewUrls.current.forEach(url => URL.revokeObjectURL(url));
    uploadPreviewUrls.current.clear();
  }, []);

  const authorizedFetch = useCallback<PropsWithFetch>((input, init) => fetchDeviceAuthorizedResource(input, init), [fetchDeviceAuthorizedResource]);

  const rememberGalleryTab = (value: string) => {
    if (value !== 'system' && value !== 'mine') return;
    setGalleryTab(value);
    try { localStorage.setItem(GALLERY_TAB_STORAGE_KEY, value); } catch { /* state still persists for this page */ }
  };

  const syncDevice = useCallback((silent = false): Promise<void> => {
    // BEGIN intentionally invalidates the committed header. Never start a
    // background preview read while an installation owns the image resource.
    if (getDeviceImageInstallState()) return Promise.resolve();
    if (syncInFlight.current) return syncInFlight.current;
    const epoch = ++syncEpoch.current;
    const isCurrent = () => syncEpoch.current === epoch && !getDeviceImageInstallState();

    const operation = (async () => {
      if (!deviceConnected || !dataIsReady) {
        if (!isCurrent()) return;
        setCurrentPreview(''); setCurrentFingerprint(''); setCurrentGalleryId(null); onAvailabilityChange(false);
        if (!deviceId) clearDeviceImagePreview(previewIdentity);
        return;
      }
      try {
        const catalog = await getDeviceImageCatalog();
        if (!isCurrent()) return;
        if (!catalog.user.valid) {
          clearDeviceImagePreview(previewIdentity);
          setCurrentPreview(''); setCurrentFingerprint(''); setCurrentGalleryId(null); onAvailabilityChange(false);
          const currentConfig = configRef.current;
          if (currentConfig.standbyDisplay === 'backgroundImage') stageDeferredScreenControl({ ...currentConfig, standbyDisplay: 'none', backgroundImageId: '' });
          return;
        }
        const expected = catalog.user.width * catalog.user.height * 2 * catalog.user.frameCount;
        if (expected !== catalog.user.size || catalog.user.frameCount < 1 || catalog.user.frameCount > 6) throw new Error('Invalid device image catalog');
        const fp = fingerprint(catalog.user);
        const cached = loadDeviceImagePreview(previewIdentity, fp);
        if (cached) {
          setCurrentPreview(cached.previewUrl);
          setCurrentFingerprint(fp);
          setCurrentGalleryId(cached.galleryImageId);
          onAvailabilityChange(true);
          return;
        }
        setCurrentFingerprint(fp);
        onAvailabilityChange(true);
        if (catalog.user.crc32 === undefined) {
          setCurrentPreview('');
          setCurrentGalleryId(null);
          return;
        }
        try {
          const image = await fetchGalleryImageMatch(authorizedFetch, {
            width: catalog.user.width,
            height: catalog.user.height,
            frameCount: catalog.user.frameCount,
            fps: catalog.user.fps,
            payloadBytes: catalog.user.size,
            payloadCrc32: catalog.user.crc32,
          });
          if (!isCurrent()) return;
          if (!image) {
            setCurrentPreview('');
            setCurrentGalleryId(null);
            return;
          }
          const previewUrl = await loadGallerySourcePreview(image, authorizedFetch);
          if (!isCurrent()) {
            URL.revokeObjectURL(previewUrl);
            return;
          }
          saveDeviceImagePreview(previewIdentity, {
            fingerprint: fp,
            previewUrl,
            galleryImageId: image.id,
          });
          setCurrentPreview(previewUrl);
          setCurrentGalleryId(image.id);
        } catch (error) {
          if (!isCurrent()) return;
          clearDeviceImagePreview(previewIdentity);
          setCurrentPreview('');
          setCurrentGalleryId(null);
          if (!silent) {
            showToast({
              title: zh ? '服务器预览加载失败' : 'Failed to load server preview',
              description: error instanceof Error ? error.message : String(error),
              type: 'error',
            });
          }
        }
      } catch (error) {
        if (!isCurrent()) return;
        clearDeviceImagePreview(previewIdentity);
        setCurrentPreview(''); setCurrentFingerprint(''); setCurrentGalleryId(null);
        onAvailabilityChange(false);
        const currentConfig = configRef.current;
        if (currentConfig.standbyDisplay === 'backgroundImage') stageDeferredScreenControl({ ...currentConfig, standbyDisplay: 'none', backgroundImageId: '' });
        if (!silent) {
          showToast({ title: zh ? '读取设备图片失败' : 'Failed to read device image', description: error instanceof Error ? error.message : String(error), type: 'error' });
        }
      }
    })();
    const tracked = operation.finally(() => {
      if (syncInFlight.current === tracked) syncInFlight.current = null;
    });
    syncInFlight.current = tracked;
    return tracked;
  }, [authorizedFetch, dataIsReady, deviceConnected, deviceId, getDeviceImageCatalog, onAvailabilityChange, previewIdentity, stageDeferredScreenControl, zh]);

  const loadSystem = useCallback(async (append = false) => {
    const result = await fetchSystemGallery(authorizedFetch, append ? systemCursor : null);
    setSystemImages(current => append ? [...current, ...result.items] : result.items);
    setSystemCursor(result.nextCursor);
  }, [authorizedFetch, systemCursor]);
  const loadMine = useCallback(async () => { if (session.authenticated) setMine(await fetchMyGallery()); else setMine({ limit: 10, count: 0, items: [] }); }, [session.authenticated]);

  useEffect(() => { void syncDevice(); }, [accountUid, syncDevice, deviceSession?.sessionId]);
  useEffect(() => {
    if (!open) return;
    setLoading(true);
    Promise.all([loadSystem(false), loadMine()]).catch(error => showToast({ title: zh ? '图库加载失败' : 'Failed to load gallery', description: error instanceof Error ? error.message : String(error), type: 'error' })).finally(() => setLoading(false));
  }, [open, session.authenticated]); // eslint-disable-line react-hooks/exhaustive-deps

  const allImages = useMemo(() => [...systemImages, ...mine.items], [mine.items, systemImages]);
  const isInstalledGalleryImage = useCallback((image: GalleryImage) => {
    if (currentGalleryId) return image.id === currentGalleryId;
    if (!currentFingerprint) return false;
    return fingerprint({
      width: image.width,
      height: image.height,
      size: image.payloadBytes,
      frameCount: image.frameCount,
      fps: image.fps,
      crc32: image.payloadCrc32,
    }) === currentFingerprint;
  }, [currentFingerprint, currentGalleryId]);

  useEffect(() => {
    if (currentGalleryId && !allImages.some(image => image.id === currentGalleryId)) setCurrentGalleryId(null);
    if (!currentGalleryId && currentFingerprint) {
      const matches = allImages.filter(image => fingerprint({ width: image.width, height: image.height, size: image.payloadBytes, frameCount: image.frameCount, fps: image.fps, crc32: image.payloadCrc32 }) === currentFingerprint);
      if (matches.length === 1) setCurrentGalleryId(matches[0].id);
    }
  }, [allImages, currentFingerprint, currentGalleryId]);

  useEffect(() => {
    setSelected(current => {
      const next = new Set([...current].filter(id => {
        const image = allImages.find(item => item.id === id);
        return image ? !isInstalledGalleryImage(image) : false;
      }));
      return next.size === current.size ? current : next;
    });
  }, [allImages, isInstalledGalleryImage]);

  useEffect(() => {
    const busy = installingId !== null;
    onBusyChange(busy);
    const handler = (event: BeforeUnloadEvent) => { if (busy) { event.preventDefault(); event.returnValue = ''; } };
    window.addEventListener('beforeunload', handler);
    return () => { window.removeEventListener('beforeunload', handler); onBusyChange(false); };
  }, [installingId, onBusyChange]);

  const install = useCallback(async (image: GalleryImage) => {
    if (!deviceConnected || installingId || currentGalleryId === image.id) return;
    // Preview synchronization no longer reads device pixels. It may be waiting
    // on an unrelated server image download, so it must never gate the
    // device-priority upload transaction. The device request queue still
    // serializes any catalog report already in flight ahead of BEGIN.
    if (!deviceConnected || getDeviceImageInstallState() || currentGalleryId === image.id) return;
    if (!beginDeviceImageInstall(image.id)) return;
    // Supersede any server preview request for the previously installed image.
    // Its completion is ignored by the epoch checks and cannot overwrite this
    // installation's state. Clear the tracked promise so failure recovery can
    // start a fresh authoritative catalog read immediately.
    syncEpoch.current += 1;
    syncInFlight.current = null;
    // Installation owns this source now, so it must leave the batch-delete
    // selection immediately even if it had been selected by a prior click.
    setSelected(current => {
      if (!current.has(image.id)) return current;
      const next = new Set(current);
      next.delete(image.id);
      return next;
    });
    let installedPreview = '';
    try {
      installedPreview = await loadGallerySourcePreview(image, authorizedFetch);
      const response = image.scope === 'system' ? await authorizedFetch(image.deviceAssetUrl) : await fetch(image.deviceAssetUrl, { credentials: 'same-origin' });
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      const bytes = new Uint8Array(await response.arrayBuffer());
      const parsed = parseUimgV3(bytes);
      const digest = await sha256Hex(bytes);
      if (digest !== image.deviceSha256 || parsed.payloadCrc32 !== image.payloadCrc32 || parsed.payloadBytes !== image.payloadBytes || parsed.frameCount !== image.frameCount || parsed.fps !== image.fps) throw new Error('Gallery image verification failed');
      await uploadDeviceImage({
        width: parsed.width, height: parsed.height, data: parsed.payload,
        frameCount: parsed.frameCount, fps: parsed.fps,
        // The client reports total only after COMMIT succeeds. Keep a small
        // visible remainder for the catalog read-back below, then finish both
        // progress indicators together after the device identity is verified.
        onProgress: (received, total) => updateDeviceImageInstall(
          image.id,
          total ? Math.min(98, Math.floor(received * 98 / total)) : 0,
        ),
      });
      const catalog = await getDeviceImageCatalog();
      if (!catalog.user.valid || catalog.user.crc32 !== parsed.payloadCrc32 ||
          catalog.user.size !== parsed.payloadBytes || catalog.user.width !== parsed.width ||
          catalog.user.height !== parsed.height || catalog.user.frameCount !== parsed.frameCount ||
          catalog.user.fps !== parsed.fps) {
        throw new Error('Device did not confirm the installed image');
      }
      const fp = fingerprint(catalog.user);
      saveDeviceImagePreview(previewIdentity, { fingerprint: fp, previewUrl: installedPreview, galleryImageId: image.id });
      setCurrentGalleryId(image.id); setCurrentFingerprint(fp); setCurrentPreview(installedPreview);
      installedPreview = '';
      updateDeviceImageInstall(image.id, 100); onAvailabilityChange(true); onInstalled();
      stageDeferredScreenControl({ ...config, standbyDisplay: 'backgroundImage', backgroundImageId: 'USER_IMAGE' });
      showToast({ title: zh ? '图片已安装到设备' : 'Image installed on device', type: 'success' });
    } catch (error) {
      // Release ownership before reconciling the directory. Recovery is
      // intentionally silent so the original upload error remains visible.
      finishDeviceImageInstall(image.id);
      await syncDevice(true);
      showToast({ title: zh ? '安装图片失败' : 'Failed to install image', description: error instanceof Error ? error.message : String(error), type: 'error' });
    } finally {
      if (installedPreview) URL.revokeObjectURL(installedPreview);
      finishDeviceImageInstall(image.id); setHold({ id: '', progress: 0, delayMs: 0, pressed: false }); installTriggered.current = false;
    }
  }, [authorizedFetch, config, currentGalleryId, deviceConnected, getDeviceImageCatalog, installingId, onAvailabilityChange, onInstalled, previewIdentity, stageDeferredScreenControl, syncDevice, uploadDeviceImage, zh]);

  useEffect(() => {
    if (!hold.id || installingId) return;
    let frame = 0;
    holdLast.current = performance.now();
    const tick = (now: number) => {
      const elapsedMs = Math.min(100, now - holdLast.current);
      holdLast.current = now;
      setHold(current => {
        if (!current.id) return current;
        const next = advanceHoldGesture(current, current.pressed, elapsedMs);
        const progress = next.progress;
        if (progress >= 1 && !installTriggered.current) {
          installTriggered.current = true;
          const image = allImages.find(item => item.id === current.id);
          if (image) queueMicrotask(() => void install(image));
        }
        return progress === 0 && !current.pressed
          ? { id: '', progress: 0, delayMs: 0, pressed: false }
          : { ...current, ...next };
      });
      frame = requestAnimationFrame(tick);
    };
    frame = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(frame);
  }, [allImages, hold.id, install, installingId]);

  const press = (id: string, pressed: boolean) => {
    if (disabled || installingId || currentGalleryId === id) return;
    installTriggered.current = false;
    setHold(current => current.id === id ? { ...current, pressed } : pressed ? { id, progress: 0, delayMs: 0, pressed: true } : current);
  };

  const toggleSelected = (id: string) => setSelected(current => {
    const image = allImages.find(item => item.id === id);
    if (!image || isInstalledGalleryImage(image) || installingId === id) return current;
    const next = new Set(current);
    if (next.has(id)) next.delete(id); else next.add(id);
    return next;
  });

  const beginPress = (id: string) => {
    pressGesture.current = {
      id,
      startedAt: performance.now(),
      resumed: hold.id === id && hold.progress > 0,
    };
    press(id, true);
  };

  const endPress = (id: string, selectable: boolean, cancelled = false) => {
    const gesture = pressGesture.current;
    pressGesture.current = null;
    press(id, false);
    const image = allImages.find(item => item.id === id);
    if (!cancelled && selectable && image && !isInstalledGalleryImage(image) && installingId !== id && gesture?.id === id &&
        isShortSelectionPress(performance.now() - gesture.startedAt, gesture.resumed)) {
      toggleSelected(id);
    }
  };

  const setUpload = (id: string, patch: Partial<UploadState>) => setUploads(current => current.map(item => item.id === id ? { ...item, ...patch } : item));
  const uploadOne = async (item: UploadState) => {
    try {
      setUpload(item.id, { status: 'processing', progress: 0, error: undefined });
      const processed = await processGalleryImage(item.file);
      setUpload(item.id, { status: 'uploading', progress: 0 });
      const created = await uploadMyGalleryImage({
        source: item.file, preview: processed.preview, deviceAsset: processed.deviceAsset,
        manifest: { title: item.file.name.replace(/\.[^.]+$/, ''), width: processed.width, height: processed.height, frameCount: processed.frameCount, fps: processed.fps, payloadCrc32: processed.payloadCrc32 },
      }, progress => setUpload(item.id, { progress }));
      setMine(current => current.items.some(image => image.id === created.id) ? current : {
        ...current,
        count: Math.min(current.limit, current.count + 1),
        items: [created, ...current.items],
      });
      setUploads(current => current.filter(upload => upload.id !== item.id));
      uploadPreviewUrls.current.delete(item.previewUrl);
      URL.revokeObjectURL(item.previewUrl);
    } catch (error) {
      const message = error instanceof Error ? error.message : String(error);
      setUpload(item.id, { status: 'error', error: message });
      showToast({ title: zh ? `上传失败：${item.file.name}` : `Upload failed: ${item.file.name}`, description: message, type: 'error' });
    }
  };
  const addFiles = async (files: File[]) => {
    const valid = files.filter(file => ['image/png', 'image/jpeg', 'image/gif'].includes(file.type) && file.size <= MAX_SOURCE_BYTES);
    if (valid.length !== files.length) showToast({ title: zh ? '仅支持 20MB 内的 PNG/JPG/GIF' : 'Only PNG/JPG/GIF files up to 20MB are supported', type: 'error' });
    const activeUploads = uploads.filter(upload => upload.status !== 'error').length;
    if (valid.length > mine.limit - mine.count - activeUploads) { showToast({ title: zh ? '所选图片超过剩余图库名额' : 'Selected files exceed the remaining gallery quota', type: 'error' }); return; }
    const items = valid.map(file => {
      const previewUrl = URL.createObjectURL(file);
      uploadPreviewUrls.current.add(previewUrl);
      return { id: crypto.randomUUID(), file, previewUrl, status: 'queued' as const, progress: 0 };
    });
    setUploads(current => [...items, ...current]);
    await mapWithConcurrency(items, 4, uploadOne);
    await loadMine();
  };

  const removeSelected = async () => {
    const ids = [...selected].filter(id => {
      const image = mine.items.find(item => item.id === id);
      return image && id !== installingId && !isInstalledGalleryImage(image);
    });
    if (!ids.length) return;
    try { await deleteMyGalleryImages(ids); setSelected(new Set()); await loadMine(); }
    catch (error) { showToast({ title: zh ? '删除图片失败' : 'Failed to delete images', description: error instanceof Error ? error.message : String(error), type: 'error' }); }
  };

  const Tile = ({ image, selectable }: { image: GalleryImage; selectable: boolean }) => {
    const isCurrent = isInstalledGalleryImage(image);
    const isInstalling = installingId === image.id;
    const progress = isInstalling
      ? deviceImageInstallRingProgress(deviceProgress ?? 0)
      : hold.id === image.id ? hold.progress : 0;
    const isSelected = selectable && !isCurrent && !isInstalling && selected.has(image.id);
    const canSelect = selectable && !isCurrent && !isInstalling;
    return <Box position="relative" borderWidth="2px" borderColor={isCurrent ? 'green.400' : isSelected ? 'blue.400' : 'gray.700'} borderRadius="md" overflow="hidden" height="118px"
      userSelect="none" touchAction="none" cursor={canSelect || (!isCurrent && !installingId) ? 'pointer' : 'default'}
      aria-disabled={isCurrent || isInstalling}
      onContextMenu={event => event.preventDefault()}
      onPointerDown={event => { if (isCurrent || isInstalling) return; event.currentTarget.setPointerCapture(event.pointerId); beginPress(image.id); }}
      onPointerUp={() => endPress(image.id, selectable)} onPointerCancel={() => endPress(image.id, selectable, true)}
      onKeyDown={event => { if (!isCurrent && !isInstalling && (event.key === ' ' || event.key === 'Enter') && !event.repeat) beginPress(image.id); }}
      onKeyUp={event => { if (event.key === ' ' || event.key === 'Enter') endPress(image.id, selectable); }} tabIndex={isCurrent || isInstalling ? -1 : 0}>
      <AuthorizedPreview image={image} fetchAuthorized={authorizedFetch} />
      <Box position="absolute" insetX="0" bottom="0" bg="blackAlpha.700" px="2" py="1"><Text fontSize="xs" truncate>{image.title}</Text></Box>
      {isSelected && <Flex position="absolute" left="2" top="2" bg="blue.500" borderRadius="full" p="1"><LuCheck /></Flex>}
      {isCurrent && <Flex position="absolute" right="2" top="2" bg="green.500" borderRadius="full" p="1"><LuCheck /></Flex>}
      {progress > 0 && !isCurrent && <Flex position="absolute" inset="0" align="center" justify="center" bg="blackAlpha.500">
        <Box
          width="54px"
          height="54px"
          borderRadius="full"
          style={{
            background: `conic-gradient(${isInstalling ? '#3b82f6' : '#22c55e'} ${progress * 360}deg, rgba(255,255,255,.2) 0)`,
            WebkitMask: 'radial-gradient(farthest-side, transparent calc(100% - 5px), #000 0)',
            mask: 'radial-gradient(farthest-side, transparent calc(100% - 5px), #000 0)',
          }}
        />
      </Flex>}
    </Box>;
  };

  const UploadTile = ({ upload }: { upload: UploadState }) => {
    const failed = upload.status === 'error';
    const progress = upload.status === 'uploading' ? upload.progress : 0;
    const retrying = retryHoverId === upload.id;
    const retry = () => { if (failed) void uploadOne(upload); };
    return <Box
      position="relative"
      borderWidth="2px"
      borderColor={failed ? 'red.500' : 'gray.700'}
      borderRadius="md"
      overflow="hidden"
      height="118px"
      cursor={failed ? 'pointer' : 'default'}
      role={failed ? 'button' : undefined}
      tabIndex={failed ? 0 : undefined}
      onClick={retry}
      onMouseEnter={() => { if (failed) setRetryHoverId(upload.id); }}
      onMouseLeave={() => setRetryHoverId(current => current === upload.id ? null : current)}
      onKeyDown={event => { if (failed && (event.key === 'Enter' || event.key === ' ')) { event.preventDefault(); retry(); } }}
    >
      <Image src={upload.previewUrl} alt={upload.file.name} width="100%" height="100%" objectFit="contain" draggable={false} />
      {!failed && <Box
        position="absolute"
        insetX="0"
        top={`${progress}%`}
        bottom="0"
        bg="blackAlpha.700"
        transition="top 120ms linear"
        pointerEvents="none"
      />}
      {!failed && upload.status !== 'uploading' && <Flex position="absolute" inset="0" align="center" justify="center"><Spinner size="sm" /></Flex>}
      {failed && <Flex position="absolute" inset="0" align="center" justify="center" bg="blackAlpha.600">
        <Text fontSize="sm" fontWeight="semibold">{retrying ? copy.retry : copy.failed}</Text>
      </Flex>}
      <Box position="absolute" insetX="0" bottom="0" bg="blackAlpha.700" px="2" py="1"><Text fontSize="xs" truncate>{upload.file.name}</Text></Box>
    </Box>;
  };

  const activeUploadCount = uploads.filter(upload => upload.status !== 'error').length;
  const addDisabled = mine.count + activeUploadCount >= mine.limit;
  const requestAdd = () => {
    if (addDisabled) {
      showToast({ title: copy.limitReached, description: copy.limitReachedDescription, type: 'warning' });
      return;
    }
    fileInput.current?.click();
  };
  const AddTile = () => <Box
    height="118px"
    borderWidth="2px"
    borderStyle="dashed"
    borderColor="gray.600"
    borderRadius="md"
    color="gray.400"
    cursor="pointer"
    opacity={addDisabled ? 0.45 : 1}
    display="flex"
    alignItems="center"
    justifyContent="center"
    role="button"
    tabIndex={0}
    aria-label={copy.add}
    aria-disabled={addDisabled}
    _hover={addDisabled ? undefined : { borderColor: 'green.400', color: 'green.400' }}
    onClick={requestAdd}
    onKeyDown={event => {
      if (event.key === 'Enter' || event.key === ' ') {
        event.preventDefault();
        requestAdd();
      }
    }}
  ><LuPlus size="42" /></Box>;

  const galleryToolbar = (showMineActions: boolean) => <HStack
    justify="space-between"
    align="center"
    mb="3"
    height="28px"
    minHeight="28px"
  >
    <Text fontSize="xs" lineHeight="20px" color="gray.500">{copy.hold}</Text>
    <Box width="160px" height="28px" flexShrink="0">
      {showMineActions && <HStack gap="2" height="28px" justify="flex-end">
        <Text fontSize="xs" lineHeight="20px" color="gray.400">{copy.quota(mine.count, mine.limit)}</Text>
        <Button
          size="xs"
          width="28px"
          height="28px"
          minWidth="28px"
          minHeight="28px"
          p="0"
          colorPalette="red"
          variant="subtle"
          title={copy.remove}
          aria-label={copy.remove}
          disabled={!selected.size}
          onClick={() => void removeSelected()}
        ><LuTrash2 /></Button>
      </HStack>}
    </Box>
  </HStack>;

  const currentName = allImages.find(image => image.id === currentGalleryId)?.title || (currentPreview ? copy.local : copy.empty);
  return <>
    <VStack width="full" gap="2">
      <Box
        width={`${DEVICE_SCREEN_WIDTH / displayPixelRatio}px`}
        height={`${DEVICE_SCREEN_HEIGHT / displayPixelRatio}px`}
        boxSizing="content-box"
        borderWidth="2px"
        borderColor="gray.600"
        padding="2px"
        transition="border-color 150ms ease"
        _hover={{ borderColor: 'green.400' }}
        position="relative"
        borderRadius="lg"
        overflow="hidden"
        bg="gray.900"
        cursor="pointer"
        onClick={() => setOpen(true)}
      >
        {currentPreview ? <Image src={currentPreview} alt={currentName} width="100%" height="100%" maxWidth="none" objectFit="cover" objectPosition="center" display="block" /> : <Flex width="100%" height="100%" align="center" justify="center"><LuImagePlus size="32" /></Flex>}
        {installingId && <Flex position="absolute" inset="0" bg="blackAlpha.700" align="center" justify="center" direction="column"><Spinner /><Text fontSize="xs" mt="2">{copy.uploading} {deviceProgress ?? 0}%</Text></Flex>}
      </Box>
      {currentPreview && <Text fontSize="xs" color="gray.400">{currentName}</Text>}
    </VStack>
    <Dialog.Root open={open} onOpenChange={details => setOpen(details.open)} size="xl">
      <Dialog.Backdrop backdropFilter="blur(4px)" />
      <Dialog.Positioner><Dialog.Content
        width="min(1080px, calc(100vw - 64px))"
        maxWidth="min(1080px, calc(100vw - 64px))"
        height="min(800px, calc(100vh - 64px))"
      >
        <Dialog.Header><Dialog.Title>{zh ? '背景图片图库' : 'Background Gallery'}</Dialog.Title></Dialog.Header>
        <Dialog.Body flex="1" minHeight="0" overflowY="auto">
          <Tabs.Root value={galleryTab} onValueChange={details => rememberGalleryTab(details.value)}>
            <Tabs.List><Tabs.Trigger value="system">{copy.system}</Tabs.Trigger><Tabs.Trigger value="mine">{copy.mine}</Tabs.Trigger></Tabs.List>
            <Tabs.Content value="system" pt="4">
              {galleryToolbar(false)}
              {loading ? <Spinner /> : <Grid templateColumns="repeat(auto-fill,minmax(180px,1fr))" gap="3">{systemImages.map(image => <Tile key={image.id} image={image} selectable={false} />)}</Grid>}
              {systemCursor && <Button mt="4" size="sm" onClick={() => void loadSystem(true)}>{copy.loadMore}</Button>}
            </Tabs.Content>
            <Tabs.Content value="mine" pt="4">
              {galleryToolbar(session.authenticated)}
              {!session.authenticated ? <Text>{copy.signIn}</Text> : <>
                <input ref={fileInput} hidden type="file" multiple accept="image/png,image/jpeg,image/gif" onChange={event => { const files = [...(event.target.files || [])]; event.target.value = ''; void addFiles(files); }} />
                <Grid templateColumns="repeat(auto-fill,minmax(180px,1fr))" gap="3">
                  <AddTile />
                  {uploads.map(upload => <UploadTile key={upload.id} upload={upload} />)}
                  {mine.items.map(image => <Tile key={image.id} image={image} selectable />)}
                </Grid>
              </>}
            </Tabs.Content>
          </Tabs.Root>
        </Dialog.Body>
        <Dialog.CloseTrigger />
      </Dialog.Content></Dialog.Positioner>
    </Dialog.Root>
  </>;
}
