'use strict';

// Release downloads are immutable; installation and recovery belong to the device.
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const Database = require('better-sqlite3');
const multer = require('multer');
const { readFlatZipEntries, validateUploadedOtaPackage } = require('./action');
const { validateInstallContract, validateInstallArtifact } = require('./release-install-contract');

const MAX_BUNDLE_BYTES = 12 * 1024 * 1024;
const ZIP_LIMITS = { maxEntrySize: 4 * 1024 * 1024, maxTotalSize: MAX_BUNDLE_BYTES, maxEntries: 6 };
const VERSION = /^(0|[1-9]\d{0,4})\.(0|[1-9]\d{0,4})\.(0|[1-9]\d{0,4})$/;
const sha256 = data => crypto.createHash('sha256').update(data).digest('hex');

class ReleaseError extends Error {
    constructor(code, message, status = 400) {
        super(message); this.code = code; this.status = status;
    }
}
function requireValue(condition, message) {
    if (!condition) throw new ReleaseError('INVALID_RELEASE', message);
}
function text(value, max, field, required = true) {
    requireValue(typeof value === 'string' && value.length <= max && (!required || value.trim()), `${field} is invalid`);
    return value.trim();
}
function validateManifest(manifest) {
    requireValue(manifest && [1, 2].includes(manifest.schemaVersion), 'Unsupported release schemaVersion');
    requireValue(manifest.product === 'XORA' && manifest.deviceModel === 'STM32H750_HBOX', 'Unsupported product or deviceModel');
    requireValue(typeof manifest.version === 'string' && VERSION.test(manifest.version) && manifest.hardwareVersion === '2.0.0', 'Invalid version or unsupported hardware');
    requireValue(manifest.bootSecurityMode === 'unlocked-development' && manifest.requiresManualLifecycleProvisioning === false,
        'Only unlocked-development without lifecycle provisioning is accepted');
    requireValue(manifest.compatibility && typeof manifest.compatibility === 'object', 'Compatibility declarations are required');
    text(manifest.compatibility.stm32Tx, 1000, 'compatibility.stm32Tx');
    text(manifest.compatibility.txRx, 1000, 'compatibility.txRx');
    requireValue(Array.isArray(manifest.artifacts) && [3, 4].includes(manifest.artifacts.length), 'STM32 A/B and TX are required; RX is optional');
    const keys = new Set(); const names = new Set(['release.json', 'release.sig']);
    for (const a of manifest.artifacts) {
        requireValue(a && ['stm32', 'tx', 'rx'].includes(a.component), 'Invalid component');
        const key = a.component === 'stm32' ? `stm32-${a.slot}` : a.component;
        requireValue(['stm32-A', 'stm32-B', 'tx', 'rx'].includes(key) && !keys.has(key), 'Duplicate or invalid component/slot');
        requireValue(a.component === 'stm32' || a.slot === undefined, 'Only STM32 artifacts have slots');
        keys.add(key);
        requireValue(typeof a.file === 'string' && /^[A-Za-z0-9][A-Za-z0-9_.-]{0,127}$/.test(a.file) && !names.has(a.file), 'Invalid or duplicate artifact filename');
        names.add(a.file);
        requireValue(typeof a.version === 'string' && VERSION.test(a.version) && a.hardwareVersion === manifest.hardwareVersion, 'Invalid component version or hardware');
        text(a.buildId, 128, 'buildId');
        requireValue(a.bootSecurityMode === 'unlocked-development' && a.requiresManualLifecycleProvisioning === false, 'Artifact build mode is not unlocked-development');
        requireValue(Number.isSafeInteger(a.size) && a.size > 0 && a.size <= ZIP_LIMITS.maxEntrySize && /^[a-f0-9]{64}$/.test(a.sha256), 'Invalid artifact size or SHA-256');
        if (a.component !== 'stm32') {
            requireValue(a.size <= 0x70000 && a.size % 4 === 0 && (a.component !== 'tx' || a.size > 0x1000), 'Invalid CH585 image boundary');
            requireValue(a.imageFormat === (a.component === 'tx' ? 'ch585-tx-combined' : 'ch585-rx-bin'), 'TX/RX image format does not match component');
        }
    }
    requireValue(keys.has('stm32-A') && keys.has('stm32-B') && keys.has('tx'), 'STM32 A/B and TX are required');
    const slots = manifest.artifacts.filter(a => a.component === 'stm32');
    requireValue(slots[0].version === slots[1].version && slots[0].buildId === slots[1].buildId, 'STM32 A/B must have the same version and buildId');
    try { validateInstallContract(manifest); } catch (error) { requireValue(false, error.message); }
    return manifest;
}

function validateBundle(file, publicKey, tempRoot) {
    requireValue(fs.statSync(file).size <= MAX_BUNDLE_BYTES, 'Release package exceeds 12 MiB');
    const entries = readFlatZipEntries(file, ZIP_LIMITS);
    const raw = entries.get('release.json'); const signature = entries.get('release.sig');
    requireValue(raw && raw.length <= 32768 && signature?.length === 64, 'release.json and raw P-256 release.sig are required');
    requireValue(publicKey && crypto.verify('sha256', raw, { key: publicKey, dsaEncoding: 'ieee-p1363' }, signature), 'Release signature is invalid or release public key is missing');
    const manifest = validateManifest(JSON.parse(raw.toString('utf8')));
    requireValue(manifest.schemaVersion !== 2 || raw.length <= 8192, 'Installation manifest exceeds 8 KiB');
    requireValue(entries.size === manifest.artifacts.length + 2, 'Package contains undeclared files');
    const work = fs.mkdtempSync(path.join(tempRoot, 'validate-'));
    try {
        for (const a of manifest.artifacts) {
            const data = entries.get(a.file);
            requireValue(data && data.length === a.size && sha256(data) === a.sha256, `Artifact digest/size mismatch: ${a.file}`);
            if (a.component === 'stm32') {
                const nested = path.join(work, a.file);
                fs.writeFileSync(nested, data);
                const slot = validateUploadedOtaPackage(nested, a.slot, publicKey);
                requireValue(slot.version === a.version && slot.hardware_version === a.hardwareVersion, `STM32 manifest mismatch: ${a.file}`);
                requireValue(slot.bootSecurityMode !== 'secure-production' && slot.requiresManualLifecycleProvisioning !== true, 'STM32 package requires forbidden lifecycle provisioning');
                if (manifest.schemaVersion === 2) validateInstallArtifact(a, data, () => readFlatZipEntries(nested), manifest.install.configWrite);
            }
            if (manifest.schemaVersion === 2 && a.component === 'tx') validateInstallArtifact(a, data);
        }
    } finally { fs.rmSync(work, { recursive: true, force: true }); }
    return manifest;
}

class FirmwareReleaseStore {
    constructor({ databasePath, assetRoot, publicKey, legacyStore = null }) {
        fs.mkdirSync(path.dirname(databasePath), { recursive: true });
        this.assetRoot = path.resolve(assetRoot);
        this.tempRoot = path.join(this.assetRoot, 'tmp');
        fs.mkdirSync(this.tempRoot, { recursive: true });
        this.publicKey = publicKey; this.legacyStore = legacyStore;
        this.db = new Database(databasePath);
        this.db.pragma('journal_mode = WAL');
        this.db.exec(`
            CREATE TABLE IF NOT EXISTS releases (
                id TEXT PRIMARY KEY, version TEXT NOT NULL, hardware TEXT NOT NULL,
                model TEXT NOT NULL, manifest TEXT NOT NULL, bundle_hash TEXT NOT NULL,
                status TEXT NOT NULL, revision INTEGER NOT NULL DEFAULT 1,
                notes TEXT NOT NULL DEFAULT '', acceptance TEXT NOT NULL DEFAULT '',
                created_at TEXT NOT NULL, published_at TEXT, reason TEXT NOT NULL DEFAULT '',
                UNIQUE(model, hardware, version)
            );
            CREATE TABLE IF NOT EXISTS release_audit (
                id INTEGER PRIMARY KEY, release_id TEXT NOT NULL, actor TEXT NOT NULL,
                action TEXT NOT NULL, at TEXT NOT NULL, before_json TEXT, after_json TEXT
            );
            CREATE TABLE IF NOT EXISTS release_imports (
                id TEXT PRIMARY KEY, status TEXT NOT NULL, release_id TEXT,
                error TEXT, created_at TEXT NOT NULL
            );
        `);
        // An interrupted import never creates a published release.
        this.db.prepare("UPDATE release_imports SET status='failed', error='Import interrupted; upload again.' WHERE status='validating'").run();
    }
    close() { this.db.close(); }
    bundlePath(hash) { return path.join(this.assetRoot, `${hash}.zip`); }
    get(id, includeAudit = false) {
        const row = this.db.prepare('SELECT * FROM releases WHERE id=?').get(id);
        if (!row) throw new ReleaseError('RELEASE_NOT_FOUND', 'Release not found', 404);
        const result = {
            id: row.id, manifest: JSON.parse(row.manifest), status: row.status, revision: row.revision,
            bundleSha256: row.bundle_hash, installable: JSON.parse(row.manifest).schemaVersion === 2,
            notes: row.notes, acceptance: row.acceptance, createdAt: row.created_at,
            publishedAt: row.published_at, reason: row.reason,
            checks: ['signature', 'artifact-digests', 'stm32-signed-packages', 'hardware', 'unlocked-development'],
        };
        if (includeAudit) result.audit = this.db.prepare('SELECT actor, action, at, before_json, after_json FROM release_audit WHERE release_id=? ORDER BY id DESC LIMIT 100').all(id)
            .map(a => ({ actor: JSON.parse(a.actor), action: a.action, at: a.at, before: JSON.parse(a.before_json), after: JSON.parse(a.after_json) }));
        return result;
    }
    list({ query = '', status = '', offset = 0, limit = 20, hardware = '' } = {}, publicOnly = false) {
        requireValue(typeof query === 'string' && typeof status === 'string' && typeof hardware === 'string', 'Invalid catalog filters');
        const count = Math.floor(Math.min(100, Math.max(1, Number(limit) || 20)));
        const start = Math.floor(Math.min(1000000, Math.max(0, Number(offset) || 0)));
        const values = { status: publicOnly ? 'published' : status, needle: `%${query.slice(0, 120)}%`, hardware };
        const where = "WHERE (@status='' OR status=@status) AND (version LIKE @needle OR hardware LIKE @needle OR notes LIKE @needle) AND (@hardware='' OR hardware=@hardware)";
        const total = this.db.prepare(`SELECT COUNT(*) AS n FROM releases ${where}`).get(values).n;
        const rows = this.db.prepare(`SELECT id FROM releases ${where} ORDER BY created_at DESC, id LIMIT @limit OFFSET @offset`).all({ ...values, limit: count, offset: start });
        return { items: rows.map(r => publicOnly ? this.publicDetail(r.id) : this.get(r.id)), total, limit: count, offset: start };
    }
    publicDetail(id) {
        const r = this.get(id);
        if (r.status !== 'published') throw new ReleaseError('RELEASE_NOT_FOUND', 'Release not found', 404);
        // Never expose draft acceptance evidence, administrators or internal paths.
        return { id: r.id, manifest: r.manifest, notes: r.notes, publishedAt: r.publishedAt, status: 'published', bundleSha256: r.bundleSha256, installable: r.installable };
    }
    download(id) {
        const release = this.publicDetail(id);
        if (!release.installable) throw new ReleaseError('CATALOG_ONLY', 'Repackage this release with an installation contract', 409);
        const file = this.bundlePath(release.bundleSha256);
        const bytes = fs.readFileSync(file);
        requireValue(sha256(bytes) === release.bundleSha256, 'Stored bundle digest mismatch');
        validateBundle(file, this.publicKey, this.tempRoot);
        // Synchronous validation and read: a replacement cannot change streamed bytes.
        return { release, bytes };
    }
    audit(id, actor, action, before, after) {
        this.db.prepare('INSERT INTO release_audit(release_id,actor,action,at,before_json,after_json) VALUES(?,?,?,?,?,?)')
            .run(id, JSON.stringify({ actorType: actor.actorType, actorId: actor.actorId }), action, new Date().toISOString(), JSON.stringify(before), JSON.stringify(after));
    }
    import(file, actor) {
        const job = crypto.randomUUID(); const now = new Date().toISOString();
        this.db.prepare("INSERT INTO release_imports(id,status,created_at) VALUES(?,'validating',?)").run(job, now);
        try {
            const manifest = validateBundle(file, this.publicKey, this.tempRoot);
            const hash = sha256(fs.readFileSync(file)); const id = crypto.randomUUID();
            const destination = this.bundlePath(hash);
            if (!fs.existsSync(destination)) fs.copyFileSync(file, destination, fs.constants.COPYFILE_EXCL);
            this.db.transaction(() => {
                this.db.prepare(`INSERT INTO releases(id,version,hardware,model,manifest,bundle_hash,status,created_at) VALUES(?,?,?,?,?,?,'draft',?)`)
                    .run(id, manifest.version, manifest.hardwareVersion, manifest.deviceModel, JSON.stringify(manifest), hash, now);
                this.audit(id, actor, 'import', null, { status: 'draft', manifest });
                this.db.prepare("UPDATE release_imports SET status='completed',release_id=? WHERE id=?").run(id, job);
            })();
            return this.importStatus(job);
        } catch (error) {
            const message = error.code === 'SQLITE_CONSTRAINT_UNIQUE' ? 'This model/hardware/version already exists; binaries cannot be replaced.' :
                error.code?.startsWith('SQLITE_') || ['EACCES', 'ENOSPC', 'ENOENT'].includes(error.code) ? 'Import storage failed; retry after checking server storage.' : error.message;
            this.db.prepare("UPDATE release_imports SET status='failed',error=? WHERE id=?").run(String(message).slice(0, 1000), job);
            return this.importStatus(job);
        }
    }
    importStatus(id) {
        const job = this.db.prepare('SELECT id,status,release_id AS releaseId,error FROM release_imports WHERE id=?').get(id);
        if (!job) throw new ReleaseError('IMPORT_NOT_FOUND', 'Import not found', 404);
        return job;
    }
    mutate(id, revision, action, input, actor) {
        return this.db.transaction(() => {
            const old = this.get(id);
            if (!Number.isInteger(revision) || old.revision !== revision) throw new ReleaseError('REVISION_CONFLICT', 'Release changed; refresh before retrying.', 409);
            let status = old.status; let notes = old.notes; let acceptance = old.acceptance; let reason = old.reason; let published = old.publishedAt;
            if (action === 'edit') {
                requireValue(status === 'draft', 'Only drafts can be edited');
                notes = text(input.notes, 10000, 'notes', false);
                acceptance = text(input.acceptance, 4000, 'acceptance', false);
            } else if (action === 'publish') {
                requireValue(['draft', 'withdrawn'].includes(status), 'Release is already published');
                requireValue(notes.trim() && acceptance.trim(), 'Release notes and acceptance evidence are required');
                const row = this.db.prepare('SELECT bundle_hash FROM releases WHERE id=?').get(id);
                const bundle = this.bundlePath(row.bundle_hash);
                requireValue(sha256(fs.readFileSync(bundle)) === row.bundle_hash, 'Stored package is corrupted');
                const verified = validateBundle(bundle, this.publicKey, this.tempRoot);
                requireValue(JSON.stringify(verified) === JSON.stringify(old.manifest), 'Stored release manifest differs from signed package');
                status = 'published'; published = new Date().toISOString(); reason = '';
            } else if (action === 'withdraw') {
                requireValue(status === 'published', 'Only published releases can be withdrawn');
                reason = text(input.reason, 1000, 'reason'); status = 'withdrawn';
            } else if (action === 'delete') {
                requireValue(status === 'draft', 'Only drafts can be deleted');
                this.audit(id, actor, action, old, null);
                this.db.prepare('DELETE FROM releases WHERE id=?').run(id);
                return null; // Content blobs are retained; never delete files referenced by another release.
            } else throw new ReleaseError('INVALID_ACTION', 'Unknown action');
            this.db.prepare('UPDATE releases SET status=?,notes=?,acceptance=?,reason=?,published_at=?,revision=revision+1 WHERE id=?')
                .run(status, notes, acceptance, reason, published, id);
            const result = this.get(id);
            this.audit(id, actor, action, old, result);
            return this.get(id, true);
        })();
    }
    legacy() {
        return (this.legacyStore?.getFirmwares() || []).map(r => ({ id: r.id, version: r.version, hardwareVersion: r.hardwareVersion, notes: r.desc || '', scope: 'STM32_ONLY' }));
    }
}

function initFirmwareReleaseRoutes(app, { store, adminAccess, deviceAccess }) {
    const base = '/api/admin/firmware';
    const manage = adminAccess.requireAdmin({ serviceScope: 'firmware.manage' });
    const human = adminAccess.requireAdmin({ humanOnly: true });
    const wrap = fn => (req, res, next) => { try { fn(req, res); } catch (error) { next(error); } };
    const ok = (res, data) => res.json({ success: true, data });
    app.use(base, (_req, res, next) => { res.set('Cache-Control', 'no-store'); next(); });
    app.get(`${base}/releases`, manage, wrap((req, res) => ok(res, store.list(req.query))));
    app.get(`${base}/legacy`, manage, wrap((_req, res) => ok(res, store.legacy())));
    app.get(`${base}/releases/:id`, manage, wrap((req, res) => ok(res, store.get(req.params.id, true))));
    app.get(`${base}/imports/:id`, manage, wrap((req, res) => ok(res, store.importStatus(req.params.id))));
    const upload = multer({ dest: store.tempRoot, limits: { fileSize: MAX_BUNDLE_BYTES, files: 1, fields: 0, parts: 2 } }).single('bundle');
    app.post(`${base}/imports`, manage, (req, res, next) => {
        upload(req, res, error => {
            if (error) return next(new ReleaseError('UPLOAD_REJECTED', 'Upload rejected: one ZIP file, at most 12 MiB.'));
            if (!req.file) return next(new ReleaseError('FILE_REQUIRED', 'Select a release ZIP file'));
            try { ok(res, store.import(req.file.path, req.authenticatedAdmin)); }
            catch (failure) { next(failure); }
            finally { fs.rmSync(req.file.path, { force: true }); }
        });
    });
    app.patch(`${base}/releases/:id`, manage, wrap((req, res) => {
        requireValue(req.body && Object.keys(req.body).every(k => ['revision', 'notes', 'acceptance'].includes(k)), 'Only notes and acceptance may be edited');
        ok(res, store.mutate(req.params.id, req.body.revision, 'edit', req.body, req.authenticatedAdmin));
    }));
    for (const action of ['publish', 'withdraw']) app.post(`${base}/releases/:id/${action}`, human,
        wrap((req, res) => ok(res, store.mutate(req.params.id, req.body?.revision, action, req.body || {}, req.authenticatedAdmin))));
    app.delete(`${base}/releases/:id`, manage, wrap((req, res) => ok(res, store.mutate(req.params.id, req.body?.revision, 'delete', {}, req.authenticatedAdmin))));
    app.use('/api/firmware-releases', deviceAccess.requireSession(['config.read']), (_req, res, next) => { res.set('Cache-Control', 'no-store'); next(); });
    app.get('/api/firmware-releases', wrap((req, res) => ok(res, store.list(req.query, true))));
    app.get('/api/firmware-releases/verification-key', wrap((_req, res) => {
        const key = store.publicKey?.type === 'public' ? store.publicKey : crypto.createPublicKey(store.publicKey);
        ok(res, key.export({ format: 'jwk' }));
    }));
    app.get('/api/firmware-releases/:id/download', wrap((req, res) => {
        const { release, bytes } = store.download(req.params.id);
        res.set('X-Content-SHA256', release.bundleSha256);
        res.type('application/zip').attachment(`XORA-${release.manifest.version}.zip`).send(bytes);
    }));
    app.get('/api/firmware-releases/:id', wrap((req, res) => ok(res, store.publicDetail(req.params.id))));
}

module.exports = { FirmwareReleaseStore, initFirmwareReleaseRoutes, validateBundle, validateManifest, MAX_BUNDLE_BYTES };
