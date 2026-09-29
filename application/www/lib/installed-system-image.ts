import { fetchGalleryImageMatch, fetchSystemGallery, type GalleryImage, type GalleryImageFingerprint } from './image-gallery';

const STORAGE_KEY = 'xora-installed-system-images-v1';
const MAX_REFERENCES = 32;
type Reference = { fingerprint: string; imageId: string };

function references(): Reference[] {
  try {
    const value: unknown = JSON.parse(localStorage.getItem(STORAGE_KEY) || '[]');
    return Array.isArray(value) ? value.filter((item): item is Reference =>
      item && typeof item.fingerprint === 'string' && typeof item.imageId === 'string' && item.imageId.length > 0,
    ).slice(-MAX_REFERENCES) : [];
  } catch { return []; }
}

function forget(fingerprint: string): void {
  try { localStorage.setItem(STORAGE_KEY, JSON.stringify(references().filter(item => item.fingerprint !== fingerprint))); } catch { /* Storage is optional. */ }
}

export function rememberInstalledSystemImage(
  fingerprint: string,
  image: Pick<GalleryImage, 'id' | 'scope'>,
): void {
  if (image.scope !== 'system') return;
  // Direct WebHID sessions have no persistent device ID. Key the association by
  // the complete read-back image fingerprint, just like server-side matching.
  // Keep only bounded metadata, never pixels or Blob URLs.
  const entries = references().filter(item => item.fingerprint !== fingerprint);
  entries.push({ fingerprint, imageId: image.id });
  try { localStorage.setItem(STORAGE_KEY, JSON.stringify(entries.slice(-MAX_REFERENCES))); } catch { /* Storage is optional. */ }
}

export async function findInstalledGalleryImage(
  authorizedFetch: (input: RequestInfo | URL, init?: RequestInit) => Promise<Response>,
  fingerprint: string,
  deviceImage: GalleryImageFingerprint,
): Promise<GalleryImage | null> {
  const saved = references().find(item => item.fingerprint === fingerprint);
  // The canonical asset remains the authority whenever its fingerprint matches.
  const matched = await fetchGalleryImageMatch(authorizedFetch, deviceImage);
  if (matched) return matched;
  if (!saved) return null;

  // Converted legacy GIFs have a different fingerprint from their stored asset.
  // Resolve their ID through the authorized, published system catalog, including
  // later pages. Never trust a URL or image bytes supplied by localStorage.
  let cursor: string | null = null;
  const visited = new Set<string>();
  do {
    const page = await fetchSystemGallery(authorizedFetch, cursor);
    const image = page.items.find(item => item.id === saved.imageId && item.scope === 'system');
    if (image) return image;
    cursor = page.nextCursor;
    if (cursor && visited.has(cursor)) throw new Error('Invalid gallery cursor');
    if (cursor) visited.add(cursor);
  } while (cursor);
  forget(fingerprint);
  return null;
}
