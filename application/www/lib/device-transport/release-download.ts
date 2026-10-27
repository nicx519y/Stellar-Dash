import type { PublicFirmwareRelease } from '../admin/firmware-types';

/** Catalog and verification key stay on the origin; public bytes carry no session. */
export function fetchReleaseBundle(
  release: PublicFirmwareRelease,
  authorizedFetch: (path: string, init: RequestInit) => Promise<Response>,
  assetBase = process.env.NEXT_PUBLIC_WEB_ASSET_BASE || '',
): Promise<Response> {
  if (!release.downloadUrl) {
    return authorizedFetch(`/api/firmware-releases/${encodeURIComponent(release.id)}/download`, { cache: 'no-store' });
  }
  const url = new URL(release.downloadUrl);
  const expectedOrigin = assetBase ? new URL(assetBase).origin : '';
  const expectedPath = `/cdn/firmware/${encodeURIComponent(release.id)}/${release.bundleSha256}/release.zip`;
  if (!expectedOrigin || url.origin !== expectedOrigin || url.protocol !== 'https:' ||
      url.username || url.password || url.search || url.hash ||
      !/^[a-f0-9]{64}$/.test(release.bundleSha256 || '') || url.pathname !== expectedPath) {
    throw new Error('Invalid firmware CDN URL');
  }
  return fetch(url.href, { method: 'GET', mode: 'cors', credentials: 'omit', redirect: 'error', cache: 'default' });
}
