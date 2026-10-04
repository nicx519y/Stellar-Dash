'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const { spawnSync } = require('node:child_process');
const express = require('express');
const { makeSignedPackage } = require('./fixtures/firmware-release-fixture');
const { storedZip, createBundle } = require('../scripts/create-firmware-bundle');
const { createFirmwareDraft, parseArguments, serverOrigin } = require('../scripts/create-firmware-draft');
const { gitReleaseNotes, initialReleaseNotes, localReleaseNotes } = require('../scripts/git-release-notes');
const { FirmwareReleaseStore, initFirmwareReleaseRoutes, validateBundle } = require('../src/firmware-releases');
const { AdminAccessService } = require('../src/admin-access');
const { createDirectDeviceAccess } = require('../src/direct-device-access');
const actor = { actorType: 'user', actorId: 'test-admin' };
const sha = b => crypto.createHash('sha256').update(b).digest('hex');

function setup(t) {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'xora-release-test-'));
    const keys = crypto.generateKeyPairSync('ec', { namedCurve: 'prime256v1' });
    const store = new FirmwareReleaseStore({ databasePath: path.join(root, 'releases.db'), assetRoot: path.join(root, 'assets'), publicKey: keys.publicKey });
    t.after(() => { store.close(); fs.rmSync(root, { recursive: true, force: true }); });
    return { root, keys, store };
}
function gitFixture(root) {
    const repo = path.join(root, 'git'); fs.mkdirSync(repo);
    const run = (...args) => {
        const result = spawnSync('git', args, { cwd: repo, encoding: 'utf8', timeout: 10000 });
        assert.equal(result.status, 0, result.stderr);
        return result.stdout.trim();
    };
    run('init', '--quiet');
    run('config', 'user.name', 'XORA Test');
    run('config', 'user.email', 'xora@example.test');
    const file = path.join(repo, 'application', 'Src', 'input', 'buttons.cpp');
    fs.mkdirSync(path.dirname(file), { recursive: true });
    fs.writeFileSync(file, 'int buttons = 1;\n');
    run('add', '.'); run('commit', '--quiet', '-m', 'Initial version');
    return { repo, run, file };
}
function identity(component, buildId, protocol = 1) {
    const result = Buffer.alloc(121);
    result.write('XORAFW2\0', 0, 'ascii'); result.writeUInt32LE(component, 8);
    result.writeUInt32LE(protocol, 12); result.writeUInt32LE(protocol, 16);
    result.writeUInt32LE(component === 1 ? 34 : 0, 20);
    result.write('1.2.3', 24); result.write(buildId, 56); return result;
}
function bundle(context, transform = () => {}, version = '2.0.0', v2 = false, protocol = 1) {
    const artifacts = []; const entries = [];
    for (const slot of ['A', 'B']) {
        const f = makeSignedPackage(slot, context.keys, v2 ? { application: identity(1, 'test-build', protocol) } : {});
        const data = storedZip([['manifest.json', Buffer.from(JSON.stringify(f.manifest))], ['metadata.bin', f.metadata], ...f.files]);
        const file = `stm32-${slot}.zip`; entries.push([file, data]);
        artifacts.push({ component: 'stm32', slot, version: f.manifest.version, buildId: 'test-build', file, size: data.length, sha256: sha(data) });
        if(v2) artifacts[artifacts.length - 1].metadataSha256 = sha(f.metadata);
    }
    const tx = Buffer.alloc(8192, 0x44); entries.push(['tx.bin', tx]);
    if(v2) identity(2, 'tx-build', protocol).copy(tx,4096);
    artifacts.push({ component: 'tx', version: '1.2.3', buildId: 'tx-build', imageFormat: 'ch585-tx-combined', file: 'tx.bin', size: tx.length, sha256: sha(tx) });
    for (const a of artifacts) Object.assign(a, { hardwareVersion: '2.0.0', bootSecurityMode: 'unlocked-development', requiresManualLifecycleProvisioning: false });
    const manifest = { schemaVersion: 1, product: 'XORA', deviceModel: 'STM32H750_HBOX', hardwareVersion: '2.0.0', version,
        bootSecurityMode: 'unlocked-development', requiresManualLifecycleProvisioning: false,
        compatibility: { stm32Tx: '1.2.x', txRx: '1.2.x' }, artifacts };
    if(v2) {
        manifest.schemaVersion=2;manifest.buildId='release-build';
        manifest.install={protocol,order:'tx-then-stm32',configRead:{min:34,max:34},configWrite:34,
            stm32Maintenance:{min:protocol,max:protocol},txMaintenance:{min:protocol,max:protocol}};
        Object.assign(artifacts[2],{applicationOffset:4096,applicationSize:4096,applicationSha256:sha(tx.subarray(4096))});
    }
    transform(manifest, entries);
    const raw = Buffer.from(JSON.stringify(manifest));
    const signature = crypto.sign('sha256', raw, { key: context.keys.privateKey, dsaEncoding: 'ieee-p1363' });
    const data = storedZip([['release.json', raw], ['release.sig', signature], ...entries]);
    const file = path.join(context.root, `${crypto.randomUUID()}.zip`); fs.writeFileSync(file, data);
    return { file, data, manifest, entries };
}

test('signed complete package stays draft until manual publish; revision, withdrawal and audit survive reopening', t => {
    const c = setup(t); const b = bundle(c);
    const job = c.store.import(b.file, actor); assert.equal(job.status, 'completed');
    let r = c.store.get(job.releaseId); assert.equal(r.status, 'draft');
    assert.equal(c.store.list({}, true).total, 0); assert.throws(() => c.store.publicDetail(r.id), /not found/);
    assert.throws(() => c.store.mutate(r.id, 1, 'publish', {}, actor), /Release notes/);
    r = c.store.mutate(r.id, 1, 'edit', { notes: 'Release notes' }, actor);
    assert.equal(r.acceptance, '');
    assert.throws(() => c.store.mutate(r.id, 1, 'publish', {}, actor), /changed/);
    r = c.store.mutate(r.id, r.revision, 'publish', {}, actor);
    assert.equal(c.store.list({}, true).total, 1);
    assert.equal(c.store.publicDetail(r.id).acceptance, undefined);
    assert.throws(() => c.store.mutate(r.id, r.revision, 'edit', { notes: 'overwrite', acceptance: 'x' }, actor), /drafts/);
    assert.throws(() => c.store.mutate(r.id, r.revision, 'withdraw', { reason: '' }, actor), /reason/);
    r = c.store.mutate(r.id, r.revision, 'withdraw', { reason: 'Regression' }, actor);
    assert.equal(c.store.list({}, true).total, 0);
    r = c.store.mutate(r.id, r.revision, 'publish', {}, actor);
    assert.equal(r.audit.length, 5); assert.equal(r.status, 'published');
    const second = new FirmwareReleaseStore({ databasePath: path.join(c.root, 'releases.db'), assetRoot: c.store.assetRoot, publicKey: c.keys.publicKey });
    assert.equal(second.get(r.id, true).audit.length, 5); second.close();
    assert.equal(c.store.import(b.file, actor).status, 'failed');
    assert.equal(c.store.list().total, 1);
});

for (const status of ['draft', 'published', 'withdrawn']) test(`delete ${status} release retains audit and content, rejects stale revisions and closes downloads`, t => {
    const c = setup(t); const b = bundle(c, () => {}, '3.0.0', true);
    const job = c.store.import(b.file, actor);
    let r = c.store.mutate(job.releaseId, 1, 'edit', { notes: 'Deletion test' }, actor);
    if (status !== 'draft') r = c.store.mutate(r.id, r.revision, 'publish', {}, actor);
    if (status === 'withdrawn') r = c.store.mutate(r.id, r.revision, 'withdraw', { reason: 'Issue' }, actor);
    assert.throws(() => c.store.mutate(r.id, r.revision - 1, 'delete', {}, actor), /changed/);
    assert.equal(c.store.get(r.id).status, status);
    c.store.mutate(r.id, r.revision, 'delete', {}, actor);
    assert.equal(c.store.list().total, 0); assert.equal(c.store.list({}, true).total, 0);
    assert.throws(() => c.store.get(r.id), /not found/);
    assert.throws(() => c.store.publicDetail(r.id), /not found/);
    assert.throws(() => c.store.download(r.id), /not found/);
    assert.deepEqual(fs.readFileSync(c.store.bundlePath(sha(b.data))), b.data);
    const audit = c.store.db.prepare("SELECT * FROM release_audit WHERE release_id=? AND action='delete'").all(r.id);
    assert.equal(audit.length, 1); assert.equal(JSON.parse(audit[0].before_json).status, status);
    assert.deepEqual(JSON.parse(audit[0].actor), actor); assert.equal(JSON.parse(audit[0].after_json), null);
    const reopened = new FirmwareReleaseStore({ databasePath: path.join(c.root, 'releases.db'), assetRoot: c.store.assetRoot, publicKey: c.keys.publicKey });
    try {
        const retry = reopened.import(b.file, actor);
        assert.equal(retry.status, status === 'draft' ? 'completed' : 'failed');
        if (status !== 'draft') {
            assert.match(retry.error, /previously published/);
            assert.equal(reopened.import(bundle(c, () => {}, '3.0.1', true).file, actor).status, 'completed');
        }
    } finally { reopened.close(); }
});

test('notes-only edits preserve historical acceptance and reject empty notes at publish', t => {
    const c = setup(t); const job = c.store.import(bundle(c).file, actor);
    let r = c.store.mutate(job.releaseId, 1, 'edit', { notes: '', acceptance: 'Historical evidence' }, actor);
    assert.throws(() => c.store.mutate(r.id, r.revision, 'publish', {}, actor), /Release notes/);
    r = c.store.mutate(r.id, r.revision, 'edit', { notes: 'Ready for release' }, actor);
    assert.equal(r.acceptance, 'Historical evidence');
    assert.equal(c.store.mutate(r.id, r.revision, 'publish', {}, actor).status, 'published');
});

test('v2 binds executable identities and permits only published immutable downloads', t => {
    const c=setup(t); const b=bundle(c,()=>{},'3.0.0',true);
    const job=c.store.import(b.file,actor); assert.equal(job.status,'completed',job.error);
    assert.throws(()=>c.store.download(job.releaseId),/not found/);
    let r=c.store.mutate(job.releaseId,1,'edit',{notes:'v2',acceptance:'host fixtures'},actor);
    r=c.store.mutate(r.id,r.revision,'publish',{},actor);
    const downloaded=c.store.download(r.id);
    assert.equal(downloaded.release.installable,true);assert.equal(sha(downloaded.bytes),downloaded.release.bundleSha256);
    c.store.mutate(r.id,r.revision,'withdraw',{reason:'test'},actor);
    assert.throws(()=>c.store.download(r.id),/not found/);
});

test('v2 rejects incompatible declaration, metadata replacement, wrong build identity and IAP ranges', t => {
    const c=setup(t);
    for(const change of [
        m=>{m.install.order='stm32-then-tx';},
        m=>{m.install.configRead={min:35,max:34};},
        m=>{m.artifacts[0].metadataSha256='0'.repeat(64);},
        m=>{m.artifacts[2].buildId='wrong-build';},
        m=>{m.artifacts[2].applicationOffset=0;},
        m=>{m.artifacts[2].applicationSha256='0'.repeat(64);},
        m=>{m.install.configWrite=33;m.install.configRead.min=33;},
    ]) assert.throws(()=>validateBundle(bundle(c,change,'3.0.0',true).file,c.keys.publicKey,c.root));
});

test('signed manifest rejects altered digests, hardware, slots, TX/RX type, protected builds and undeclared files', t => {
    const c = setup(t);
    const failures = [
        m => { m.artifacts[0].sha256 = '0'.repeat(64); },
        m => { m.hardwareVersion = '1.0.0'; },
        m => { m.artifacts[1].slot = 'A'; },
        m => { m.artifacts[2].imageFormat = 'ch585-rx-bin'; },
        m => { m.bootSecurityMode = 'secure-production'; },
        m => { m.artifacts[2].requiresManualLifecycleProvisioning = true; },
        (m, e) => { e.push(['extra.bin', Buffer.alloc(1)]); },
        (m, e) => { e.push(['../escape.bin', Buffer.alloc(1)]); },
        (m, e) => { e.push(e[0]); },
        m => { m.artifacts[1].version = '9.0.0'; },
        (m, e) => { e[0][1][100] ^= 1; m.artifacts[0].sha256 = sha(e[0][1]); },
    ];
    for (const transform of failures) {
        const j = c.store.import(bundle(c, transform).file, actor);
        assert.equal(j.status, 'failed', String(transform)); assert.ok(j.error);
    }
    const b = bundle(c); const wrong = crypto.generateKeyPairSync('ec', { namedCurve: 'prime256v1' });
    assert.throws(() => validateBundle(b.file, wrong.publicKey, c.store.tempRoot), /signature/);
    assert.equal(c.store.list().total, 0);
    assert.deepEqual(fs.readdirSync(c.store.tempRoot), []);
});

test('publish revalidates stored bytes; database failure cannot expose orphaned content', t => {
    const c = setup(t); const b = bundle(c); const j = c.store.import(b.file, actor);
    const r = c.store.mutate(j.releaseId, 1, 'edit', { notes: 'Notes', acceptance: 'Evidence' }, actor);
    fs.appendFileSync(c.store.bundlePath(sha(b.data)), 'corrupt');
    assert.throws(() => c.store.mutate(r.id, r.revision, 'publish', {}, actor), /corrupted/);
    assert.equal(c.store.get(r.id).status, 'draft');
    c.store.db.exec("CREATE TRIGGER fail_insert BEFORE INSERT ON releases BEGIN SELECT RAISE(ABORT, 'simulated storage failure'); END;");
    assert.equal(c.store.import(bundle(c, () => {}, '2.1.0').file, actor).status, 'failed');
    assert.equal(c.store.list({}, true).total, 0);
});

test('archive entry count and inflated size limits reject malformed ZIPs before import', t => {
    const c = setup(t); const b = bundle(c);
    const invalid = path.join(c.root, 'invalid.zip');
    fs.writeFileSync(invalid, Buffer.from('partial upload'));
    assert.equal(c.store.import(invalid, actor).status, 'failed');
    const oversized = Buffer.from(b.data);
    // First central entry claims more than the bounded entry allocation.
    const central = oversized.indexOf(Buffer.from([0x50, 0x4b, 0x01, 0x02]), b.data.length - 1024);
    assert.ok(central >= 0); oversized.writeUInt32LE(16 * 1024 * 1024, central + 24);
    fs.writeFileSync(invalid, oversized);
    assert.equal(c.store.import(invalid, actor).status, 'failed');
    const tooMany = storedZip(Array.from({ length: 7 }, (_, i) => [`file${i}`, Buffer.alloc(1)]));
    fs.writeFileSync(invalid, tooMany); assert.equal(c.store.import(invalid, actor).status, 'failed');
    assert.equal(c.store.list({}, true).total, 0);
});

test('offline packaging tool produces a verifiable bundle and never overwrites output', t => {
    const c = setup(t); const b = bundle(c);
    for (const [name, data] of b.entries) fs.writeFileSync(path.join(c.root, name), data);
    const source = path.join(c.root, 'source.json'); fs.writeFileSync(source, JSON.stringify(b.manifest));
    const key = path.join(c.root, 'key.pem'); fs.writeFileSync(key, c.keys.privateKey.export({ type: 'pkcs8', format: 'pem' }));
    const output = path.join(c.root, 'output.zip'); createBundle(source, key, output);
    assert.equal(validateBundle(output, c.keys.publicKey, c.store.tempRoot).version, '2.0.0');
    assert.throws(() => createBundle(source, key, output), /EEXIST/);
});

test('HTTP flow: real admin gate, service-token limits, private drafts, publish, browse and withdraw', { timeout: 20000 }, async t => {
    const c = setup(t); const serviceToken = `stsvc_${'a'.repeat(43)}`;
    let adminEnabled = true;
    const adminAccess = new AdminAccessService({ store: {
        findServiceTokenByHash: hash => hash === sha(serviceToken) ? { id: 'service', name: 'CI', scopes: ['firmware.manage'], revokedAt: null, expiresAt: Date.now() + 60000 } : null,
        recordServiceTokenUse() {},
    }, emailAuth: {
        readSessionToken: req => req.get('Cookie'),
        resolveSession: token => token === 'admin' && adminEnabled ? { uid: 'admin', email: 'admin@example.test', role: 'admin' } : token === 'user' ? { uid: 'user', role: 'user' } : null,
        requireOrigin: origin => { if (origin !== 'http://localhost:3001') { const e = new Error('Invalid origin'); e.status = 403; throw e; } },
    } });
    const app = express(); app.use(express.json());
    initFirmwareReleaseRoutes(app, { store: c.store, adminAccess, deviceAccess: createDirectDeviceAccess({}) });
    app.use((e, _req, res, _next) => res.status(e.status || 500).json({ success: false, error: e.code, message: e.message }));
    const server = app.listen(0, '127.0.0.1'); await new Promise(resolve => server.once('listening', resolve));
    t.after(() => { server.closeAllConnections(); return new Promise(resolve => server.close(resolve)); });
    const url = `http://127.0.0.1:${server.address().port}`; const base = '/api/admin/firmware';
    const call = (p, opts = {}) => fetch(url + p, { ...opts, headers: { Cookie: 'admin', Origin: 'http://localhost:3001', ...opts.headers }, signal: AbortSignal.timeout(5000) });
    assert.equal((await call(base + '/releases', { headers: { Cookie: '' } })).status, 401);
    assert.equal((await call(base + '/releases', { headers: { Cookie: 'user' } })).status, 403);
    assert.equal((await call(base + '/imports', { method: 'POST', headers: { Origin: 'https://evil.example' } })).status, 403);
    assert.equal((await call(base + '/imports', { method: 'POST' })).status, 400);
    const signed = bundle(c, () => {}, '3.0.0', true);
    const form = new FormData(); form.append('bundle', new Blob([signed.data]), 'release.zip');
    const response = await call(base + '/imports', { method: 'POST', body: form, headers: { Authorization: `Bearer ${serviceToken}` } });
    assert.equal(response.status, 200); const job = (await response.json()).data; assert.equal(job.status, 'completed');
    const id = job.releaseId;
    const json = (data, headers = {}) => ({ method: 'POST', body: JSON.stringify(data), headers: { 'Content-Type': 'application/json', ...headers } });
    const remove = (revision, headers = {}) => call(`${base}/releases/${id}`, { ...json({ revision }, headers), method: 'DELETE' });
    assert.equal((await remove(1, { Cookie: '' })).status, 401);
    assert.equal((await remove(1, { Cookie: 'user' })).status, 403);
    assert.equal((await remove(1, { Origin: 'https://evil.example' })).status, 403);
    assert.equal((await call(`${base}/releases/${id}`, { ...json({ revision: 1, version: '9.9.9' }), method: 'PATCH' })).status, 400);
    assert.equal((await call(`/api/firmware-releases/${id}`)).status, 404);
    assert.equal((await call(`/api/firmware-releases/${id}/download`)).status, 404);
    let r = (await (await call(`${base}/releases/${id}`, { ...json({ revision: 1, notes: 'Visible' }), method: 'PATCH' })).json()).data;
    assert.equal(r.acceptance, '');
    assert.equal((await call(`${base}/releases/${id}/publish`, json({ revision: r.revision }, { Authorization: `Bearer ${serviceToken}` }))).status, 403);
    r = (await (await call(`${base}/releases/${id}/publish`, json({ revision: r.revision }))).json()).data;
    assert.equal((await remove(r.revision, { Authorization: `Bearer ${serviceToken}` })).status, 403);
    assert.equal((await remove(r.revision - 1)).status, 409);
    const catalog = await call('/api/firmware-releases', { headers: { Cookie: '' } }); assert.equal(catalog.status, 200);
    assert.equal(catalog.headers.get('cache-control'), 'no-store');
    const payload = await catalog.json(); assert.equal(payload.data.total, 1); assert.equal(payload.data.items[0].acceptance, undefined);
    const download = await call(`/api/firmware-releases/${id}/download`, { headers: { Cookie: '' } });
    assert.equal(download.status, 200); assert.equal(download.headers.get('x-content-sha256'), sha(signed.data));
    assert.deepEqual(Buffer.from(await download.arrayBuffer()), signed.data);
    const verification = await call('/api/firmware-releases/verification-key', { headers: { Cookie: '' } });
    assert.equal((await verification.json()).data.crv, 'P-256');
    assert.equal((await call(`${base}/releases/${id}/withdraw`, json({ revision: r.revision, reason: 'Issue' }))).status, 200);
    assert.equal((await call(`/api/firmware-releases/${id}`)).status, 404);
    assert.equal((await call('/downloads/' + id + '.zip')).status, 404);
    assert.equal((await call(`/api/firmware-releases/${id}/download`)).status, 404);
    const withdrawn = c.store.get(id);
    assert.equal((await remove(withdrawn.revision, { Authorization: `Bearer ${serviceToken}` })).status, 403);
    assert.equal((await remove(withdrawn.revision)).status, 200);
    assert.equal((await call(`${base}/releases/${id}`)).status, 404);
    assert.equal((await call(`/api/firmware-releases/${id}/download`)).status, 404);
    const draftJob = c.store.import(bundle(c, () => {}, '3.0.1').file, actor);
    assert.equal((await call(`${base}/releases/${draftJob.releaseId}`, { ...json({ revision: 1 }, { Authorization: `Bearer ${serviceToken}` }), method: 'DELETE' })).status, 200);
    adminEnabled = false; assert.equal((await call(base + '/releases')).status, 401);
    assert.deepEqual(fs.readdirSync(c.store.tempRoot), []);
});

test('draft command packages v2, writes concise notes and uploads an editable unpublished draft', { timeout: 20000 }, async t => {
    const c = setup(t); const git = gitFixture(c.root); const fixture = bundle(c, () => {}, '3.4.5', true, 2);
    const input = path.join(c.root, 'input'); fs.mkdirSync(input);
    for (const [name, data] of fixture.entries) fs.writeFileSync(path.join(input, name), data);
    const source = path.join(input, 'release-source.json');
    fs.writeFileSync(source, JSON.stringify(fixture.manifest));
    const signingKey = path.join(c.root, 'signing-key.pem');
    fs.writeFileSync(signingKey, c.keys.privateKey.export({ type: 'pkcs8', format: 'pem' }));
    const serviceToken = `stsvc_${'c'.repeat(43)}`;
    const serviceTokenFile = path.join(c.root, 'service-token.txt'); fs.writeFileSync(serviceTokenFile, serviceToken);
    const adminAccess = new AdminAccessService({ store: {
        findServiceTokenByHash: value => value === sha(serviceToken)
            ? { id: 'draft-cli', name: 'Draft CLI', scopes: ['firmware.manage'], revokedAt: null, expiresAt: Date.now() + 60000 }
            : null,
        recordServiceTokenUse() {},
    }, emailAuth: { readSessionToken() {}, resolveSession() { return null; }, requireOrigin() {} } });
    const app = express(); app.use(express.json());
    initFirmwareReleaseRoutes(app, { store: c.store, adminAccess, deviceAccess: createDirectDeviceAccess({}) });
    app.use((error, _req, res, _next) => res.status(error.status || 500).json({ success: false, message: error.message }));
    const server = app.listen(0, '127.0.0.1'); await new Promise(resolve => server.once('listening', resolve));
    t.after(() => { server.closeAllConnections(); return new Promise(resolve => server.close(resolve)); });
    const origin = `http://127.0.0.1:${server.address().port}`;
    const result = await createFirmwareDraft({ source, signingKey, outDir: path.join(c.root, 'output'),
        server: origin, serviceTokenFile, initialRelease: true, gitRepo: git.repo });
    const draft = c.store.get(result.releaseId, true);
    assert.equal(draft.status, 'draft');
    assert.equal(draft.manifest.install.protocol, 2);
    assert.equal(draft.notes, initialReleaseNotes({ repoRoot: git.repo, version: '3.4.5' }).notes);
    assert.equal(draft.acceptance, '');
    assert.equal(draft.bundleSha256, result.bundleSha256);
    assert.equal(c.store.list({}, true).total, 0);
    assert.deepEqual(draft.audit.map(entry => entry.action), ['edit', 'import']);
    assert.equal(fs.readFileSync(result.notesPath, 'utf8').trim(), draft.notes);
    assert.equal(JSON.parse(fs.readFileSync(result.evidencePath, 'utf8')).kind, 'initial-release');
    assert.equal(sha(fs.readFileSync(result.bundlePath)), draft.bundleSha256);
    assert.equal(result.adminUrl, `${origin}/admin/firmware/`);
    assert.ok(draft.notes.trim());
});

test('draft command accepts only local admin servers and rejects contradictory Git selection', () => {
    assert.throws(() => serverOrigin('http://firmware.st-dash.com'), /loopback/);
    assert.throws(() => serverOrigin('https://firmware.st-dash.com'), /loopback/);
    assert.throws(() => serverOrigin('http://localhost:3001/other'), /origin/);
    assert.equal(serverOrigin('http://localhost:3001'), 'http://localhost:3001');
    assert.equal(parseArguments(['--source', 'x', '--signing-key', 'y', '--out-dir', 'z',
        '--service-token-file', 'token']).server, 'http://localhost:3001');
    assert.throws(() => parseArguments(['--source', 'x', '--signing-key', 'y', '--out-dir', 'z', '--initial-release', '--since', 'v1', '--dry-run']), /cannot be used/);
});

test('draft command explains missing release source before creating output', async t => {
    const c = setup(t);
    const outDir = path.join(c.root, 'output');
    await assert.rejects(createFirmwareDraft({ source: 'path/to/release-source.json',
        signingKey: 'path/to/signing-key.pem', outDir, initialRelease: true, dryRun: true }),
    /--source is an example path/);
    assert.equal(fs.existsSync(outDir), false);
    await assert.rejects(createFirmwareDraft({ source: 'path/to/release-source.json',
        signingKey: 'path/to/signing-key.pem', outDir, server: 'https://firmware.st-dash.com',
        serviceTokenFile: 'path/to/token.txt', initialRelease: true }), /loopback/);
    assert.equal(fs.existsSync(outDir), false);
});

test('later version summarizes committed device changes since previous version tag', t => {
    const c = setup(t); const git = gitFixture(c.root);
    const baseline = git.run('rev-parse', 'HEAD');
    git.run('tag', 'xora-v1.0.0');
    fs.writeFileSync(git.file, 'int buttons = 2;\n');
    fs.writeFileSync(path.join(git.repo, 'README.md'), 'Unrelated hosted change\n');
    git.run('add', '.'); git.run('commit', '--quiet', '-m', 'Improve input behavior');
    const generated = gitReleaseNotes({ repoRoot: git.repo, version: '1.1.0' });
    assert.equal(generated.evidence.previousRef, 'xora-v1.0.0');
    assert.deepEqual(generated.evidence.changedSourceFiles, ['application/Src/input/buttons.cpp']);
    assert.match(generated.notes, /按键与输入相关体验持续打磨/);
    assert.equal(gitReleaseNotes({ repoRoot: git.repo, version: '1.1.0', since: baseline }).evidence.previousCommit, baseline);
    fs.writeFileSync(git.file, 'int buttons = 3;\n');
    assert.throws(() => gitReleaseNotes({ repoRoot: git.repo, version: '1.2.0' }), /Commit device firmware/);
});

test('draft CLI dry run creates a verified bundle and notes without contacting admin', t => {
    const c = setup(t); const git = gitFixture(c.root); const fixture = bundle(c, () => {}, '3.4.6', true);
    const input = path.join(c.root, 'input'); fs.mkdirSync(input);
    for (const [name, data] of fixture.entries) fs.writeFileSync(path.join(input, name), data);
    const source = path.join(input, 'release-source.json');
    fs.writeFileSync(source, JSON.stringify(fixture.manifest));
    const signingKey = path.join(c.root, 'signing-key.pem');
    fs.writeFileSync(signingKey, c.keys.privateKey.export({ type: 'pkcs8', format: 'pem' }));
    const outDir = path.join(c.root, 'dry-run');
    const result = spawnSync(process.execPath, [path.join(__dirname, '../scripts/create-firmware-draft.js'),
        '--source', source, '--signing-key', signingKey, '--out-dir', outDir,
        '--initial-release', '--git-repo', git.repo, '--dry-run'], { encoding: 'utf8', timeout: 20000 });
    assert.equal(result.status, 0, result.stderr);
    assert.match(result.stdout, /nothing was uploaded/);
    assert.ok(fs.existsSync(path.join(outDir, 'XORA-3.4.6-release.zip')));
    assert.match(fs.readFileSync(path.join(outDir, 'XORA-3.4.6-release-notes.md'), 'utf8'), /初版发布/);
    assert.equal(c.store.list().total, 0);
});

test('protocol 2 verifies readback-capable executable identities and unused hosted resources', t => {
    const c=setup(t);const b=bundle(c,()=>{},'4.0.0',true,2);
    assert.equal(c.store.import(b.file,actor).status,'completed');
    assert.throws(()=>validateBundle(bundle(c,m=>{m.install.protocol=1;m.install.stm32Maintenance=m.install.txMaintenance={min:1,max:1};},'4.0.1',true,2).file,c.keys.publicKey,c.root),/identity/);
    assert.throws(()=>validateBundle(bundle(c,m=>{m.install.txMaintenance={min:1,max:1};},'4.0.2',true,2).file,c.keys.publicKey,c.root),/maintenance/);
});

test('protocol 2 rejects a metadata resource reservation that is not explicitly unused', () => {
    const {validateInstallArtifact}=require('../src/release-install-contract');
    for(const [size,active,optional] of [[1,1,1],[0,0,0],[0,1,1]]) {
        const metadata=Buffer.alloc(807);metadata.write('webresources',303);metadata.writeUInt32LE(size,403);metadata[472]=active;metadata[747]=optional;
        const artifact={component:'stm32',metadataSha256:sha(metadata)};
        assert.throws(()=>validateInstallArtifact(artifact,Buffer.alloc(0),()=>new Map([['metadata.bin',metadata]]),34,2),/unused hosted webresources/);
    }
});


test('local package notes compare actual Git working-tree snapshots without manual input',t=>{
 const c=setup(t);const g=gitFixture(c.root);const history=path.join(c.root,'history');
 fs.mkdirSync(history);fs.writeFileSync(g.file,'int buttons = 2;\n');
 const first=localReleaseNotes({repoRoot:g.repo,version:'1.0.0',historyRoot:history,initialRelease:true});
 assert.equal(first.evidence.dirty,true);assert.match(first.evidence.sourceHashes['application/Src/input/buttons.cpp'],/^[a-f0-9]{64}$/);
 const folder=path.join(history,'XORA-1.0.0-20260101-010101','package');fs.mkdirSync(folder,{recursive:true});
 fs.writeFileSync(path.join(folder,'XORA-1.0.0-release.zip'),'local package');
 fs.writeFileSync(path.join(folder,'XORA-1.0.0-release-notes-source.json'),JSON.stringify(first.evidence));
 // The source was already dirty in the prior release; unchanged input must not
 // be described again. A new file is included in the comparison automatically.
 const display=path.join(g.repo,'application/Src/display/screen.cpp');fs.mkdirSync(path.dirname(display),{recursive:true});
 fs.writeFileSync(display,'int display = 1;\n');
 const next=localReleaseNotes({repoRoot:g.repo,version:'1.0.1',historyRoot:history});
 assert.equal(next.evidence.kind,'local-source-diff');assert.equal(next.evidence.baselineVersion,'1.0.0');
 assert.deepEqual(next.evidence.changedSourceFiles,['application/Src/display/screen.cpp']);
 assert.match(next.notes,/屏幕显示与视觉反馈/);assert.doesNotMatch(next.notes,/按键与输入/);
 fs.unlinkSync(display);
 const same=localReleaseNotes({repoRoot:g.repo,version:'1.0.1',historyRoot:history});
 assert.deepEqual(same.evidence.changedSourceFiles,[]);assert.match(same.notes,/升级流程测试/);
 // Legacy local evidence has a Git commit only: retain the explicit fallback.
 fs.writeFileSync(path.join(folder,'XORA-1.0.0-release-notes-source.json'),JSON.stringify({currentCommit:g.run('rev-parse','HEAD')}));
 const legacy=localReleaseNotes({repoRoot:g.repo,version:'1.0.1',historyRoot:history});
 assert.equal(legacy.evidence.kind,'git-worktree');assert.equal(legacy.evidence.baselineVersion,'1.0.0');
 assert.deepEqual(legacy.evidence.changedSourceFiles,['application/Src/input/buttons.cpp']);
});
