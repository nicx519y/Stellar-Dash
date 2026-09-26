export type GalleryScope = 'system' | 'user';

export type GalleryImage = {
  id: string;
  scope: GalleryScope;
  title: string;
  sourceUrl: string;
  previewUrl: string;
  deviceAssetUrl: string;
  sourceMime: string;
  width: number;
  height: number;
  frameCount: number;
  fps: number;
  payloadBytes: number;
  payloadCrc32: number;
  deviceSha256: string;
  sortOrder: number;
  published: boolean;
  createdAt: number;
  updatedAt: number;
};

export type GalleryImageFingerprint = {
  width: number;
  height: number;
  frameCount: number;
  fps: number;
  payloadBytes: number;
  payloadCrc32: number;
};

type Envelope<T> = { success?: boolean; data?: T; error?: string; message?: string };

async function readEnvelope<T>(response: Response): Promise<T> {
  const body = await response.json() as Envelope<T>;
  if (!response.ok || body.success !== true || body.data === undefined) {
    throw new Error(body.message || body.error || `HTTP ${response.status}`);
  }
  return body.data;
}

export async function fetchSystemGallery(
  authorizedFetch: (input: RequestInfo | URL, init?: RequestInit) => Promise<Response>,
  cursor?: string | null,
): Promise<{ items: GalleryImage[]; nextCursor: string | null }> {
  const query = cursor ? `?limit=30&cursor=${encodeURIComponent(cursor)}` : '?limit=30';
  return readEnvelope(await authorizedFetch(`/api/gallery/system${query}`));
}

export async function fetchGalleryImageMatch(
  authorizedFetch: (input: RequestInfo | URL, init?: RequestInit) => Promise<Response>,
  fingerprint: GalleryImageFingerprint,
): Promise<GalleryImage | null> {
  const query = new URLSearchParams({
    width: String(fingerprint.width),
    height: String(fingerprint.height),
    frameCount: String(fingerprint.frameCount),
    fps: String(fingerprint.fps),
    payloadBytes: String(fingerprint.payloadBytes),
    payloadCrc32: String(fingerprint.payloadCrc32 >>> 0),
  });
  const result = await readEnvelope<{ item: GalleryImage | null }>(
    await authorizedFetch(`/api/gallery/match?${query}`, { cache: 'no-store' }),
  );
  return result.item;
}

export async function fetchMyGallery(): Promise<{ limit: number; count: number; items: GalleryImage[] }> {
  return readEnvelope(await fetch('/api/gallery/mine', { credentials: 'same-origin' }));
}

export async function deleteMyGalleryImages(imageIds: string[]): Promise<{ deletedIds: string[] }> {
  return readEnvelope(await fetch('/api/gallery/mine/images', {
    method: 'DELETE',
    credentials: 'same-origin',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ imageIds }),
  }));
}

export function uploadMyGalleryImage(
  input: { source: File; preview: Blob; deviceAsset: Uint8Array; manifest: Record<string, unknown> },
  onProgress: (percent: number) => void,
): Promise<GalleryImage> {
  const form = new FormData();
  form.append('source', input.source, input.source.name);
  form.append('preview', input.preview, 'preview.png');
  form.append('deviceAsset', new Blob([input.deviceAsset], { type: 'application/octet-stream' }), 'device.uimg');
  form.append('manifest', JSON.stringify(input.manifest));
  return new Promise((resolve, reject) => {
    const request = new XMLHttpRequest();
    request.open('POST', '/api/gallery/mine/images');
    request.withCredentials = true;
    request.upload.onprogress = event => {
      if (event.lengthComputable) onProgress(Math.min(99, Math.floor(event.loaded * 100 / event.total)));
    };
    request.onerror = () => reject(new Error('Gallery upload failed'));
    request.onload = () => {
      try {
        const body = JSON.parse(request.responseText) as Envelope<GalleryImage>;
        if (request.status < 200 || request.status >= 300 || body.success !== true || !body.data) {
          throw new Error(body.message || body.error || `HTTP ${request.status}`);
        }
        onProgress(100);
        resolve(body.data);
      } catch (error) { reject(error); }
    };
    request.send(form);
  });
}
