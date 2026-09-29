// Included only by the isolated Mock runtime. Keys and packages never leave the browser.
import JSZip from 'jszip';
import { calculateSHA256 } from '../firmware-utils';
import type { FirmwareRelease, PublicFirmwareRelease } from './firmware-types';
let keys: Promise<CryptoKeyPair> | undefined;
const keyPair = () => keys ??= crypto.subtle.generateKey({ name: 'ECDSA', namedCurve: 'P-256' }, true, ['sign', 'verify']) as Promise<CryptoKeyPair>;
const packages = new Map<string, Promise<{ release: PublicFirmwareRelease; bytes: Uint8Array }>>();
const field = (bytes: Uint8Array, at: number, value: string) => bytes.set(new TextEncoder().encode(value), at);
async function signed(bytes: Uint8Array) { return new Uint8Array(await crypto.subtle.sign({ name: 'ECDSA', hash: 'SHA-256' }, (await keyPair()).privateKey, bytes)); }
async function archive(files: Record<string, Uint8Array>) { const zip = new JSZip(); for (const [name, data] of Object.entries(files)) zip.file(name, data); return zip.generateAsync({ type: 'uint8array', compression: 'STORE' }); }
export async function mockVerificationKey() { return crypto.subtle.exportKey('jwk', (await keyPair()).publicKey); }
export function mockInstallPackage(source: FirmwareRelease): Promise<{ release: PublicFirmwareRelease; bytes: Uint8Array }> {
  const cacheKey = `${source.id}:${source.revision}`;
  let result = packages.get(cacheKey);
  if (!result) { result = build(source); packages.set(cacheKey, result); }
  return result;
}
async function build(source: FirmwareRelease) {
  const manifest = structuredClone(source.manifest);
  manifest.schemaVersion = 2; manifest.buildId = `mock-${manifest.version}`;
  manifest.install ??= { protocol: 1, order: 'tx-then-stm32', configRead: { min: 34, max: 34 }, configWrite: 34,
    stm32Maintenance: { min: 1, max: 1 }, txMaintenance: { min: 1, max: 1 } };
  const files: Record<string, Uint8Array> = {};
  for (const artifact of manifest.artifacts) {
    artifact.version = manifest.version; artifact.buildId = manifest.buildId;
    if (artifact.component === 'stm32') {
      const app = new TextEncoder().encode(`XORA MOCK ${manifest.version} ${artifact.slot}`), adc = new Uint8Array([1, 2, 3, 4]);
      const metadata = new Uint8Array(807), view = new DataView(metadata.buffer);
      view.setUint32(0, 0x48424f58, true); view.setUint32(4, 1, true); view.setUint32(12, 807, true);
      field(metadata, 20, manifest.version); metadata[52] = artifact.slot === 'A' ? 0 : 1;
      field(metadata, 89, manifest.deviceModel); view.setUint32(121, 0x20000, true); view.setUint32(125, 0x10000, true); view.setUint32(129, 3, true);
      const base = artifact.slot === 'A' ? 0x90000000 : 0x902b0000;
      const parts = [{ name: 'application', file: 'app.bin', address: base, data: app },
        { name: 'webresources', file: '', address: base + 0x100000, data: new Uint8Array() },
        { name: 'adc_mapping', file: 'adc.bin', address: base + 0x280000, data: adc }];
      for (const [index, part] of parts.entries()) {
        const off = 133 + index * 170; field(metadata, off, part.name); field(metadata, off + 32, part.file);
        view.setUint32(off + 96, part.address, true); view.setUint32(off + 100, part.data.length, true);
        field(metadata, off + 104, part.data.length ? await calculateSHA256(part.data) : '0'.repeat(64)); metadata[off + 169] = part.data.length ? 1 : 0;
      }
      view.setUint32(739, 1, true); view.setUint32(743, 1, true); metadata[747] = 1;
      const canonical = metadata.slice(); metadata.set(await signed(canonical), 675);
      files[artifact.file] = await archive({ 'metadata.bin': metadata, 'app.bin': app, 'adc.bin': adc });
      artifact.metadataSha256 = await calculateSHA256(metadata);
    } else {
      files[artifact.file] = new Uint8Array(8192).fill(42);
      artifact.applicationOffset = 4096; artifact.applicationSize = 4096;
      artifact.applicationSha256 = await calculateSHA256(files[artifact.file].slice(4096));
    }
    artifact.size = files[artifact.file].length; artifact.sha256 = await calculateSHA256(files[artifact.file]);
  }
  const raw = new TextEncoder().encode(JSON.stringify(manifest));
  const bytes = await archive({ 'release.json': raw, 'release.sig': await signed(raw), ...files });
  return { bytes, release: { id: source.id, manifest, status: 'published' as const, notes: source.notes, publishedAt: source.publishedAt,
    installable: true, bundleSha256: await calculateSHA256(bytes) } };
}
