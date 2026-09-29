'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const express = require('express');
const { makeSignedPackage } = require('./fixtures/firmware-release-fixture');
const { storedZip, createBundle } = require('../scripts/create-firmware-bundle');
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
function identity(component, buildId) {
    const result = Buffer.alloc(121);
    result.write('XORAFW2\0', 0, 'ascii'); result.writeUInt32LE(component, 8);
    result.writeUInt32LE(1, 12); result.writeUInt32LE(1, 16);
    result.writeUInt32LE(component === 1 ? 34 : 0, 20);
    result.write('1.2.3', 24); result.write(buildId, 56); return result;
}
function bundle(context, transform = () => {}, version = '2.0.0', v2 = false) {
    const artifacts = []; const entries = [];
    for (const slot of ['A', 'B']) {
        const f = makeSignedPackage(slot, context.keys, v2 ? { application: identity(1, 'test-build') } : {});
        const data = storedZip([['manifest.json', Buffer.from(JSON.stringify(f.manifest))], ['metadata.bin', f.metadata], ...f.files]);
        const file = `stm32-${slot}.zip`; entries.push([file, data]);
        artifacts.push({ component: 'stm32', slot, version: f.manifest.version, buildId: 'test-build', file, size: data.length, sha256: sha(data) });
        if(v2) artifacts[artifacts.length - 1].metadataSha256 = sha(f.metadata);
    }
    const tx = Buffer.alloc(8192, 0x44); entries.push(['tx.bin', tx]);
    if(v2) identity(2, 'tx-build').copy(tx,4096);
    artifacts.push({ component: 'tx', version: '1.2.3', buildId: 'tx-build', imageFormat: 'ch585-tx-combined', file: 'tx.bin', size: tx.length, sha256: sha(tx) });
    for (const a of artifacts) Object.assign(a, { hardwareVersion: '2.0.0', bootSecurityMode: 'unlocked-development', requiresManualLifecycleProvisioning: false });
    const manifest = { schemaVersion: 1, product: 'XORA', deviceModel: 'STM32H750_HBOX', hardwareVersion: '2.0.0', version,
        bootSecurityMode: 'unlocked-development', requiresManualLifecycleProvisioning: false,
        compatibility: { stm32Tx: '1.2.x', txRx: '1.2.x' }, artifacts };
    if(v2) {
        manifest.schemaVersion=2;manifest.buildId='release-build';
        manifest.install={protocol:1,order:'tx-then-stm32',configRead:{min:34,max:34},configWrite:34,
            stm32Maintenance:{min:1,max:1},txMaintenance:{min:1,max:1}};
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
    assert.throws(() => c.store.mutate(r.id, 1, 'publish', {}, actor), /evidence/);
    r = c.store.mutate(r.id, 1, 'edit', { notes: 'Release notes', acceptance: 'Test evidence' }, actor);
    assert.throws(() => c.store.mutate(r.id, 1, 'publish', {}, actor), /changed/);
    r = c.store.mutate(r.id, r.revision, 'publish', {}, actor);
    assert.equal(c.store.list({}, true).total, 1);
    assert.equal(c.store.publicDetail(r.id).acceptance, undefined);
    assert.throws(() => c.store.mutate(r.id, r.revision, 'edit', { notes: 'overwrite', acceptance: 'x' }, actor), /drafts/);
    assert.throws(() => c.store.mutate(r.id, r.revision, 'delete', {}, actor), /drafts/);
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
    assert.equal((await call(`${base}/releases/${id}`, { ...json({ revision: 1, version: '9.9.9' }), method: 'PATCH' })).status, 400);
    assert.equal((await call(`/api/firmware-releases/${id}`)).status, 404);
    assert.equal((await call(`/api/firmware-releases/${id}/download`)).status, 404);
    let r = (await (await call(`${base}/releases/${id}`, { ...json({ revision: 1, notes: 'Visible', acceptance: 'Private evidence' }), method: 'PATCH' })).json()).data;
    assert.equal((await call(`${base}/releases/${id}/publish`, json({ revision: r.revision }, { Authorization: `Bearer ${serviceToken}` }))).status, 403);
    r = (await (await call(`${base}/releases/${id}/publish`, json({ revision: r.revision }))).json()).data;
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
    adminEnabled = false; assert.equal((await call(base + '/releases')).status, 401);
    assert.deepEqual(fs.readdirSync(c.store.tempRoot), []);
});
