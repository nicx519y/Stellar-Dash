import JSZip from 'jszip';
import type { FirmwareRelease, FirmwareReleaseManifest, FirmwareRuntime, ReleasePage, ReleaseQuery } from './firmware-types';

const KEY = 'xora-preview-firmware-releases-v1';
function fixture(version: string, status: FirmwareRelease['status']): FirmwareRelease {
  const manifest: FirmwareReleaseManifest = {
    schemaVersion: 1, product: 'XORA', deviceModel: 'STM32H750_HBOX', version, hardwareVersion: '2.0.0',
    bootSecurityMode: 'unlocked-development', requiresManualLifecycleProvisioning: false,
    compatibility: { stm32Tx: 'STM32 2.x ↔ TX 2.x (preview)', txRx: 'TX 2.x ↔ RX 2.x (preview)' },
    artifacts: [
      { component: 'stm32', slot: 'A', file: 'slot-a.zip' },
      { component: 'stm32', slot: 'B', file: 'slot-b.zip' },
      { component: 'tx', file: 'tx.bin', imageFormat: 'ch585-tx-combined' },
    ].map(a => ({ ...a, version, buildId: 'preview', hardwareVersion: '2.0.0', size: 131072, sha256: 'a'.repeat(64), bootSecurityMode: 'unlocked-development', requiresManualLifecycleProvisioning: false })) as FirmwareReleaseManifest['artifacts'],
  };
  return { id: `preview-${version}`, manifest, status, revision: 1, notes: 'XORA 固件发布预览 / Firmware release preview', acceptance: 'Mock preview only — not hardware acceptance.', checks: ['mock-preview'], createdAt: new Date().toISOString(), publishedAt: status === 'published' ? new Date().toISOString() : null, reason: '', audit: [] };
}
function read(): FirmwareRelease[] {
  const value = sessionStorage.getItem(KEY);
  if (value) return JSON.parse(value);
  const items = [fixture('2.1.0', 'draft'), fixture('2.0.0', 'published')]; write(items); return items;
}
function write(items: FirmwareRelease[]) { sessionStorage.setItem(KEY, JSON.stringify(items)); }
function find(id: string) { const r = read().find(r => r.id === id); if (!r) throw new Error('Release not found'); return r; }
function page<T extends FirmwareRelease>(items: T[], q: ReleaseQuery = {}): ReleasePage<T> {
  const matches = items.filter(r => (!q.status || r.status === q.status) && (!q.hardware || r.manifest.hardwareVersion === q.hardware) && (!q.query || `${r.manifest.version} ${r.notes}`.includes(q.query)));
  const offset = q.offset || 0;
  return { items: matches.slice(offset, offset + 20), total: matches.length, limit: 20, offset };
}
function change(id: string, revision: number, action: string, update: (r: FirmwareRelease) => void) {
  const items = read(); const r = items.find(r => r.id === id);
  if (!r || r.revision !== revision) throw new Error('Release changed; refresh before retrying.');
  const before = { status: r.status, revision: r.revision, notes: r.notes, acceptance: r.acceptance }; update(r); r.revision++;
  r.audit = [{ actor: { actorType: 'user', actorId: 'mock-admin' }, action, at: new Date().toISOString(), before, after: { status: r.status, revision: r.revision } }, ...(r.audit || [])];
  write(items); return r;
}
export const firmwareRuntime: FirmwareRuntime = {
  async list(q) { return page(read(), q); },
  async detail(id) { return find(id); },
  async catalog(q) {
    const p = page(read().filter(r => r.status === 'published'), q);
    return { ...p, items: p.items.map(r => ({ id: r.id, manifest: r.manifest, status: 'published', notes: r.notes, publishedAt: r.publishedAt })) };
  },
  async legacy() { return []; },
  async importBundle(file, onProgress) {
    try {
      if (file.size > 12 * 1024 * 1024) throw new Error('Package exceeds 12 MiB');
      onProgress(50);
      const zip = await JSZip.loadAsync(file); const raw = await zip.file('release.json')?.async('string');
      if (!raw || !zip.file('release.sig')) throw new Error('release.json and release.sig are required');
      const manifest = JSON.parse(raw) as FirmwareReleaseManifest;
      if (manifest.schemaVersion !== 1 || manifest.product !== 'XORA' || !Array.isArray(manifest.artifacts)) throw new Error('Invalid release manifest');
      if (read().some(r => r.manifest.version === manifest.version && r.manifest.hardwareVersion === manifest.hardwareVersion)) throw new Error('Version already exists');
      const r = { ...fixture(manifest.version, 'draft'), id: crypto.randomUUID(), manifest };
      write([r, ...read()]); onProgress(100);
      return { id: crypto.randomUUID(), status: 'completed', releaseId: r.id, error: null };
    } catch (error) { return { id: crypto.randomUUID(), status: 'failed', releaseId: null, error: String(error) }; }
  },
  async edit(id, revision, notes, acceptance) {
    return change(id, revision, 'edit', r => { if (r.status !== 'draft') throw new Error('Only drafts may be edited'); r.notes = notes; r.acceptance = acceptance; });
  },
  async publish(id, revision) {
    return change(id, revision, 'publish', r => { if (r.status === 'published' || !r.notes.trim() || !r.acceptance.trim()) throw new Error('Notes and acceptance evidence are required'); r.status = 'published'; r.publishedAt = new Date().toISOString(); r.reason = ''; });
  },
  async withdraw(id, revision, reason) {
    return change(id, revision, 'withdraw', r => { if (r.status !== 'published' || !reason.trim()) throw new Error('Withdrawal reason is required'); r.status = 'withdrawn'; r.reason = reason; });
  },
  async remove(id, revision) {
    const r = find(id); if (r.status !== 'draft' || r.revision !== revision) throw new Error('Only the current draft can be deleted');
    write(read().filter(r => r.id !== id));
  },
};
