import JSZip from 'jszip';
import { saveInstallTask, forgetInstallTask } from './release-install-task';
import { accumulateInstallProgress } from './release-install-progress';
import { calculateSHA256 } from '../firmware-utils';
import type { PublicFirmwareRelease, FirmwareReleaseManifest } from '../admin/firmware-types';
import type { DeviceCommandClient } from './device-command-client';
import { DeviceTransportError } from './types';

export interface FirmwareInventory {
  protocol: number; deviceModel: string; hardwareVersion: string; currentSlot: 'A' | 'B';
  configVersion: number; securityVersion: number; metadataConsistent: boolean;
  stm32: { version: string; buildId: string; maintenance: number; protocol: number };
  tx?: { version: string; buildId: string; maintenance: number; protocol: number };
  installationState: 'unknown' | 'installed' | 'mixed' | 'incomplete' | 'restored';
  confirmedVersion: string; confirmedDigest: string; sessionId: string;
  phase: string; targetVersion: string; targetDigest: string; error: string;
  backupReceived?: number; backupTotal?: number; backupReady?: boolean;
  recoveryResult?: 'none' | 'restoring' | 'restored' | 'failed';
  installError?: string; recoveryError?: string; errorCode?: string; restoreAttempts?: number;
  canAbort: boolean; canRetry: boolean; txReceived: number;
}
export interface ReleaseProgress {
  stage: string; component?: string; received?: number; total?: number;
  stageReceived?: number; stageTotal?: number; overallPercent?: number; stepIndex?: number;
  sessionId?: string; digest?: string; activatedAt?: number;
}
export interface PreparedRelease {
  release: PublicFirmwareRelease; digest: string; declaration: Uint8Array;
  targetSlot: 'A' | 'B'; securityVersion: number;
  components: Array<{ name: string; address: number; data: Uint8Array }>;
}
export function isLegacyPhysicalConfirmationRejection(error: unknown): boolean {
  if (!(error instanceof DeviceTransportError) || error.code !== 'protocol') return false;
  const response = error.cause as { command?: unknown; errNo?: unknown } | undefined;
  return response?.command === 'begin_release_install' && response.errNo === 423;
}
const inRange = (value: number, range: { min: number; max: number }) =>
  Number.isInteger(value) && Number.isInteger(range.min) && Number.isInteger(range.max) && value >= range.min && value <= range.max;

export function releaseBlockReason(release: PublicFirmwareRelease, inventory: FirmwareInventory | null): string | null {
  const m = release.manifest, c = m.install;
  if (m.schemaVersion !== 2 || !c || release.installable !== true) return 'catalog-only';
  if (!inventory) return 'connect-device';
  if (inventory.protocol !== 2 || inventory.stm32.protocol !== 2 || inventory.tx?.protocol !== 2) return 'baseline-required';
  if (!inventory.metadataConsistent) return 'metadata-mismatch';
  if (!['idle', 'completed', 'aborted', 'restored'].includes(inventory.phase)) return 'installation-pending';
  if (m.deviceModel !== inventory.deviceModel || m.hardwareVersion !== inventory.hardwareVersion) return 'hardware-mismatch';
  if (m.bootSecurityMode !== 'unlocked-development' || m.requiresManualLifecycleProvisioning !== false) return 'forbidden-build';
  if (c.protocol !== 2 || c.order !== 'tx-then-stm32') return 'unsupported-protocol';
  if (!inRange(inventory.configVersion, c.configRead) || c.configWrite !== inventory.configVersion) return 'configuration-incompatible';
  if (!inRange(inventory.stm32.maintenance, c.stm32Maintenance) || !inRange(inventory.tx.maintenance, c.txMaintenance)) return 'maintenance-incompatible';
  return null;
}

async function api<T>(client: DeviceCommandClient, path: string): Promise<T> {
  const response = await client.authorizedFetch(path, { cache: 'no-store' }, ['config.read']);
  const body = await response.json();
  if (!response.ok || body.success !== true) throw new Error(body.message || 'Release is unavailable');
  return body.data as T;
}
async function unzip(bytes: Uint8Array, maxEntries: number): Promise<JSZip> {
  if (bytes.length > 12 * 1024 * 1024) throw new Error('Package exceeds 12 MiB');
  // Bound declared inflated sizes before asking JSZip to decompress payloads.
  // Signed SHA-256 checks below are the content integrity authority.
  const zip = await JSZip.loadAsync(bytes);
  const entries = Object.values(zip.files);
  if (entries.length > maxEntries || entries.some(e => e.dir || !/^[A-Za-z0-9][A-Za-z0-9_.-]{0,127}$/.test(e.name))) throw new Error('Invalid archive entries');
  let total = 0;
  for (const entry of entries) {
    const size = (entry as unknown as { _data: { uncompressedSize: number } })._data.uncompressedSize;
    if (!Number.isSafeInteger(size) || size > 4 * 1024 * 1024) throw new Error('Archive entry is too large');
    total += size;
  }
  if (total > 12 * 1024 * 1024) throw new Error('Inflated package is too large');
  return zip;
}
const read = async (zip: JSZip, name: string) => {
  const entry = zip.file(name); if (!entry) throw new Error(`Missing ${name}`);
  return entry.async('uint8array');
};
const fixed = (bytes: Uint8Array, start: number, length: number) => {
  const field = bytes.slice(start, start + length); const end = field.indexOf(0);
  if (end < 0) throw new Error('Unterminated metadata field');
  return new TextDecoder('utf-8', { fatal: true }).decode(field.slice(0, end));
};

export async function downloadRelease(client: DeviceCommandClient, id: string, inventory: FirmwareInventory, progress: (p: ReleaseProgress) => void = () => {}): Promise<PreparedRelease> {
  const release = await api<PublicFirmwareRelease>(client, `/api/firmware-releases/${encodeURIComponent(id)}`);
  const reason = releaseBlockReason(release, inventory); if (reason) throw new Error(reason);
  const response = await client.authorizedFetch(`/api/firmware-releases/${encodeURIComponent(id)}/download`, { cache: 'no-store' }, ['config.read']);
  if (!response.ok) throw new Error('Release download rejected');
  const total = Number(response.headers.get('Content-Length')) || undefined;
  if (total && total > 12 * 1024 * 1024) throw new Error('Package exceeds 12 MiB');
  const chunks: Uint8Array[] = []; let received = 0;
  if (response.body) {
    const reader = response.body.getReader();
    try { while (true) {
      const chunk = await reader.read(); if (chunk.done) break;
      received += chunk.value.length;
      if (received > 12 * 1024 * 1024) { await reader.cancel(); throw new Error('Package exceeds 12 MiB'); }
      chunks.push(chunk.value); progress({ stage: 'downloading', received, total });
    } } finally { reader.releaseLock(); }
  } else { chunks.push(new Uint8Array(await response.arrayBuffer())); received = chunks[0].length; }
  const bytes = new Uint8Array(received); let position = 0;
  for (const chunk of chunks) { bytes.set(chunk, position); position += chunk.length; }
  progress({ stage: 'extracting', received: 0, total: 1 });
  if (await calculateSHA256(bytes) !== release.bundleSha256 || response.headers.get('X-Content-SHA256') !== release.bundleSha256) throw new Error('Bundle digest mismatch');
  const zip = await unzip(bytes, 6);
  const raw = await read(zip, 'release.json'), signature = await read(zip, 'release.sig');
  if (raw.length > 8192 || signature.length !== 64) throw new Error('Invalid release declaration');
  const jwk = await api<JsonWebKey>(client, '/api/firmware-releases/verification-key');
  const key = await crypto.subtle.importKey('jwk', jwk, { name: 'ECDSA', namedCurve: 'P-256' }, false, ['verify']);
  if (!await crypto.subtle.verify({ name: 'ECDSA', hash: 'SHA-256' }, key, signature, raw)) throw new Error('Release signature rejected');
  const manifest = JSON.parse(new TextDecoder('utf-8', { fatal: true }).decode(raw)) as FirmwareReleaseManifest;
  if (JSON.stringify(manifest) !== JSON.stringify(release.manifest)) throw new Error('Catalog and signed declaration differ');
  if (Object.keys(zip.files).length !== manifest.artifacts.length + 2) throw new Error('Undeclared package content');
  const artifacts = new Map<string, Uint8Array>();
  for (const artifact of manifest.artifacts) {
    if (artifacts.has(artifact.file)) throw new Error('Duplicate artifact');
    const data = await read(zip, artifact.file);
    if (data.length !== artifact.size || await calculateSHA256(data) !== artifact.sha256) throw new Error('Artifact digest mismatch');
    artifacts.set(artifact.file, data);
  }
  const targetSlot = inventory.currentSlot === 'A' ? 'B' : 'A';
  const stm = manifest.artifacts.find(a => a.component === 'stm32' && a.slot === targetSlot);
  const tx = manifest.artifacts.find(a => a.component === 'tx');
  if (!stm || !tx) throw new Error('Incomplete release');
  const inner = await unzip(artifacts.get(stm.file)!, 5);
  const metadata = await read(inner, 'metadata.bin');
  if (metadata.length !== 807 || await calculateSHA256(metadata) !== stm.metadataSha256) throw new Error('Metadata binding mismatch');
  const view = new DataView(metadata.buffer, metadata.byteOffset, metadata.byteLength);
  const canonical = metadata.slice(); canonical.fill(0, 16, 20); canonical.fill(0, 643, 739);
  if (!await crypto.subtle.verify({ name: 'ECDSA', hash: 'SHA-256' }, key, metadata.slice(675, 739), canonical)) throw new Error('STM32 signature rejected');
  if (view.getUint32(0, true) !== 0x48424f58 || view.getUint32(129, true) !== 3 || metadata[52] !== (targetSlot === 'A' ? 0 : 1) || fixed(metadata, 20, 32) !== stm.version) throw new Error('Invalid STM32 metadata');
  const securityVersion = view.getUint32(743, true);
  if (securityVersion < inventory.securityVersion) throw new Error('Security version downgrade is prohibited');
  const components: PreparedRelease['components'] = [];
  const base = targetSlot === 'A' ? 0x90000000 : 0x902b0000;
  const layouts: Record<string, [number, number]> = { application: [base, 0x100000], webresources: [base + 0x100000, 0x180000], adc_mapping: [base + 0x280000, 0x20000] };
  const seen = new Set<string>();
  for (let i = 0; i < 3; i++) {
    const off = 133 + i * 170, name = fixed(metadata, off, 32), file = fixed(metadata, off + 32, 64);
    const address = view.getUint32(off + 96, true), size = view.getUint32(off + 100, true), layout = layouts[name];
    if (!layout || seen.has(name) || address !== layout[0] || size > layout[1]) throw new Error('Invalid component layout');
    seen.add(name);
    if (name === 'webresources') {
      if (!metadata[off + 169] && !size && metadata[747] === 1) continue;
      throw new Error('TX backup requires unused hosted webresources');
    }
    const data = await read(inner, file);
    if (!metadata[off + 169] || !size || data.length !== size || await calculateSHA256(data) !== fixed(metadata, off + 104, 65)) throw new Error('Component digest mismatch');
    components.push({ name, address, data });
  }
  const txBytes = artifacts.get(tx.file)!;
  if (tx.applicationOffset !== 4096 || tx.applicationSize !== txBytes.length - 4096 || await calculateSHA256(txBytes.slice(4096)) !== tx.applicationSha256) throw new Error('Invalid TX application');
  components.push({ name: 'tx', address: 0x90790000, data: txBytes });
  const declaration = new Uint8Array(signature.length + metadata.length + raw.length);
  declaration.set(signature); declaration.set(metadata, 64); declaration.set(raw, 64 + 807);
  progress({ stage: 'extracting', received: 1, total: 1 });
  return { release, digest: await calculateSHA256(raw), declaration, targetSlot, securityVersion, components };
}

export async function installRelease(client: DeviceCommandClient, pkg: PreparedRelease, progress: (p: ReleaseProgress) => void): Promise<void> {
  const inventory = await client.request('get_firmware_inventory', {}) as unknown as FirmwareInventory;
  const reason = releaseBlockReason(pkg.release, inventory);
  if (reason || inventory.currentSlot === pkg.targetSlot || inventory.securityVersion > pkg.securityVersion) throw new Error(reason || 'Device changed after preflight');
  // Last server check precedes any device write. Recovery after activation is offline.
  const current = await api<PublicFirmwareRelease>(client, `/api/firmware-releases/${encodeURIComponent(pkg.release.id)}`);
  if (current.bundleSha256 !== pkg.release.bundleSha256) throw new Error('Release changed');
  const sessionId = `rel-${crypto.randomUUID().replaceAll('-', '').slice(0, 27)}`;
  const begin = await client.request('begin_release_install', { session_id: sessionId, declaration_size: pkg.declaration.length });
  if (!begin?.success) throw new Error('Installation rejected');
  progress({ stage: 'declaring', sessionId });
  const controllerTotal = pkg.components.filter(c => c.name !== 'tx').reduce((sum, c) => sum + c.data.length, 0);
  let controllerReceived = 0;
  for (const component of [{ name: 'declaration', address: 0, data: pkg.declaration }, ...pkg.components]) {
    const totalChunks = Math.ceil(component.data.length / 4096);
    for (let offset = 0, index = 0; offset < component.data.length; offset += 4096, index++) {
      const data = component.data.slice(offset, offset + 4096);
      const ack = await client.uploadFirmwareChunk({ sessionId, componentName: component.name, chunkIndex: index, totalChunks,
        chunkOffset: offset, targetAddress: component.address + offset, checksumSha256: await calculateSHA256(data), data });
      if (!ack.success) throw new Error(ack.error || 'Chunk rejected');
      const controller = component.name !== 'declaration' && component.name !== 'tx';
      if (controller) controllerReceived += data.length;
      progress({ stage: component.name === 'declaration' ? 'declaring' : component.name === 'tx' ? 'staging-tx' : 'staging-controller', component: component.name, received: offset + data.length, total: component.data.length,
        ...(controller ? { stageReceived: controllerReceived, stageTotal: controllerTotal } : {}) });
    }
    if (component.name === 'declaration') {
      const started = await client.request('backup_release_tx', { session_id: sessionId });
      if (!started?.success) throw new Error('TX backup rejected');
      const deadline = Date.now() + 600_000;
      while (true) {
        const status = await client.request('get_release_install_status', {}) as unknown as FirmwareInventory;
        if (status.sessionId !== sessionId || status.targetDigest !== pkg.digest) throw new Error('TX backup transaction mismatch');
        progress({ stage: 'backing-up-tx', received: status.backupReceived, total: status.backupTotal, sessionId });
        if (status.phase === 'receiving' && status.backupReady) break;
        if (status.phase !== 'backing-up-tx') throw new Error(status.error || 'TX backup failed');
        if (Date.now() >= deadline) throw new Error('TX backup timed out; new TX has not been written');
        await new Promise(resolve => setTimeout(resolve, 500));
      }
    }
  }
  const prepared = await client.request('prepare_release_install', { session_id: sessionId });
  if (!prepared?.success) throw new Error('Device did not confirm prepared images');
  progress({ stage: 'prepared' });
  // Do not abort on an uncertain activation response: the device may already be writing TX.
  const activatedAt = Date.now();
  // Storage failure blocks activation; refreshing the page must never lose the transaction.
  saveInstallTask({ protocol: 2, sessionId, digest: pkg.digest, version: pkg.release.manifest.version,
    activatedAt, release: pkg.release, result: 'waiting', progress: accumulateInstallProgress(null, { stage: 'activating' }) });
  progress({ stage: 'activating', sessionId, digest: pkg.digest, activatedAt });
  try {
    const activated = await client.request('activate_release_install', { session_id: sessionId });
    if (!activated?.success) { forgetInstallTask(sessionId); throw new Error('Activation rejected'); }
  } catch (error) {
    const response = error instanceof DeviceTransportError ? error.cause as { errNo?: number } : undefined;
    if (error instanceof DeviceTransportError && error.code === 'protocol' && response?.errNo) {
      forgetInstallTask(sessionId); throw error;
    }
    if (error instanceof Error && error.message === 'Activation rejected') throw error;
    // An absent ACK does not prove rejection. Observe this persisted transaction only.
  }
  progress({ stage: 'waiting-device', sessionId, digest: pkg.digest, activatedAt });
}
