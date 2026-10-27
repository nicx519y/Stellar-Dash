/** Public build assets only. API, uploads and the service worker stay same-origin. */
export function publicAssetUrl(assetPath: string, base = process.env.NEXT_PUBLIC_WEB_ASSET_BASE || ''): string {
  if (!base || !/^\/(?:images|fonts)\//.test(assetPath)) return assetPath;
  if (assetPath.includes('\\') || assetPath.split('/').some(part => part === '..' || part === '.')) {
    throw new Error('Invalid public asset path');
  }
  return base + assetPath;
}
