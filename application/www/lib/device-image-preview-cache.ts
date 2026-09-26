export type DeviceImagePreviewCacheIdentity = {
  deviceId: string | null;
  sessionId: string | null;
};

export type DeviceImagePreviewCacheEntry = {
  fingerprint: string;
  previewUrl: string;
  galleryImageId: string | null;
};

const STORAGE_PREFIX = 'hbox-device-image-preview-v1:';
const memoryCache = new Map<string, DeviceImagePreviewCacheEntry>();

function cacheKey(identity: DeviceImagePreviewCacheIdentity): string | null {
  if (identity.deviceId) return `device:${identity.deviceId}`;
  if (identity.sessionId) return `session:${identity.sessionId}`;
  return null;
}

function storageKey(deviceId: string): string {
  return `${STORAGE_PREFIX}${encodeURIComponent(deviceId)}`;
}

function validEntry(value: unknown): value is DeviceImagePreviewCacheEntry {
  if (!value || typeof value !== 'object') return false;
  const entry = value as Partial<DeviceImagePreviewCacheEntry>;
  return typeof entry.fingerprint === 'string' && entry.fingerprint.length > 0 &&
    typeof entry.previewUrl === 'string' &&
    (entry.previewUrl.startsWith('blob:') || entry.previewUrl.startsWith('data:image/')) &&
    (entry.galleryImageId === null || typeof entry.galleryImageId === 'string');
}

function releasePreview(entry: DeviceImagePreviewCacheEntry | null | undefined): void {
  if (entry?.previewUrl.startsWith('blob:') && typeof URL !== 'undefined') {
    URL.revokeObjectURL(entry.previewUrl);
  }
}

export function loadDeviceImagePreview(
  identity: DeviceImagePreviewCacheIdentity,
  fingerprint: string,
): DeviceImagePreviewCacheEntry | null {
  const key = cacheKey(identity);
  if (!key) return null;
  const entry = memoryCache.get(key) ?? null;
  if (!entry || entry.fingerprint !== fingerprint) {
    clearDeviceImagePreview(identity);
    return null;
  }
  return entry;
}

export function saveDeviceImagePreview(
  identity: DeviceImagePreviewCacheIdentity,
  entry: DeviceImagePreviewCacheEntry,
): void {
  const key = cacheKey(identity);
  if (!key || !validEntry(entry)) return;
  const previous = memoryCache.get(key);
  if (previous?.previewUrl !== entry.previewUrl) releasePreview(previous);
  memoryCache.set(key, entry);
  // Original gallery files may be up to 20 MB. Keep their Blob URLs only in
  // memory and let the authenticated HTTP cache handle reloads; localStorage
  // is both too small and the wrong place for account-owned image bytes.
  if (identity.deviceId && typeof localStorage !== 'undefined') {
    try { localStorage.removeItem(storageKey(identity.deviceId)); } catch { /* legacy cleanup only */ }
  }
}

export function clearDeviceImagePreview(identity: DeviceImagePreviewCacheIdentity): void {
  const key = cacheKey(identity);
  if (key) {
    releasePreview(memoryCache.get(key));
    memoryCache.delete(key);
  }
  if (identity.deviceId && typeof localStorage !== 'undefined') {
    try { localStorage.removeItem(storageKey(identity.deviceId)); } catch {
      // Ignore unavailable browser storage.
    }
  }
}

export function clearDeviceImagePreviewMemory(): void {
  memoryCache.forEach(releasePreview);
  memoryCache.clear();
}
