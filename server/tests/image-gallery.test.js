'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('fs');
const os = require('os');
const path = require('path');
const http = require('node:http');
const crypto = require('node:crypto');
const express = require('express');
const {
    ImageGalleryStore,
    LocalGalleryStorage,
    parseUimg,
    crc32,
    initImageGalleryRoutes,
} = require('../src/image-gallery');

function makeUimg(frameCount = 1, version = 3, fps = version === 4 ? 6 : 3) {
    const frameSize = 320 * 172 * 2;
    const payload = Buffer.alloc(frameSize * frameCount, 0x5a);
    const result = Buffer.alloc(4096 + payload.length);
    result.writeUInt32LE(0x474d4955, 0);
    result.writeUInt16LE(version, 4);
    result.writeUInt8(1, 6);
    result.writeUInt8(frameCount === 1 ? 1 : 2, 7);
    result.writeUInt16LE(320, 8);
    result.writeUInt16LE(172, 10);
    result.writeUInt8(frameCount, 12);
    result.writeUInt8(frameCount === 1 ? 0 : fps, 13);
    result.writeUInt32LE(frameSize, 16);
    result.writeUInt32LE(4096, 20);
    result.writeUInt32LE(payload.length, 24);
    const indexedFrames = version === 4 ? 12 : 10;
    const idOffset = 28 + indexedFrames * 4;
    for (let index = 0; index < indexedFrames; index += 1) result.writeUInt32LE(index < frameCount ? 4096 + index * frameSize : 0, 28 + index * 4);
    result.write('USER_IMAGE\0', idOffset, 'ascii');
    result.writeUInt32LE(crc32(payload), idOffset + 16);
    result.writeUInt32LE(crc32(result.subarray(0, idOffset + 20)), idOffset + 20);
    payload.copy(result, 4096);
    return result;
}

function imageInput(id, ownerUid = 'user-a') {
    return {
        id, scope: 'user', ownerUid, title: id,
        sourceKey: `${id}/source.png`, previewKey: `${id}/preview.png`, deviceKey: `${id}/device.uimg`,
        sourceMime: 'image/png', width: 320, height: 172, frameCount: 1, fps: 0,
        payloadBytes: 320 * 172 * 2, payloadCrc32: 1, deviceSha256: 'a'.repeat(64),
    };
}

function request(server, method, requestPath, headers = {}) {
    return new Promise((resolve, reject) => {
        const address = server.address();
        const req = http.request({ host: '127.0.0.1', port: address.port, method, path: requestPath, headers }, response => {
            const chunks = [];
            response.on('data', chunk => chunks.push(chunk));
            response.on('end', () => resolve({ status: response.statusCode, json: JSON.parse(Buffer.concat(chunks).toString('utf8')) }));
        });
        req.on('error', reject);
        req.end();
    });
}

test('UIMG validation accepts old six-frame and new twelve-frame assets, and rejects tampering', () => {
    const parsed = parseUimg(makeUimg(6));
    assert.equal(parsed.frameCount, 6);
    assert.equal(parsed.fps, 3);
    const twelve = parseUimg(makeUimg(12, 4));
    assert.equal(twelve.frameCount, 12);
    assert.equal(twelve.fps, 6);
    assert.equal(parseUimg(makeUimg(12, 4, 3)).fps, 3);
    assert.throws(() => parseUimg(makeUimg(6, 3, 6)), /metadata/);
    assert.throws(() => parseUimg(makeUimg(12, 4, 5)), /metadata/);
    assert.equal(twelve.payloadBytes, 320 * 172 * 2 * 12);
    const tampered = makeUimg();
    tampered[tampered.length - 1] ^= 1;
    assert.throws(() => parseUimg(tampered), /CRC32/);
    const paddedId = makeUimg();
    paddedId[83] = 1;
    paddedId.writeUInt32LE(crc32(paddedId.subarray(0, 88)), 88);
    assert.throws(() => parseUimg(paddedId), /metadata/);
    const thirteenth = makeUimg(13, 4);
    assert.throws(() => parseUimg(thirteenth), /metadata|size/);
});

test('official gallery has no account image quota and paginates', t => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'hbox-gallery-system-'));
    const store = new ImageGalleryStore({ databasePath: path.join(root, 'gallery.sqlite3') });
    t.after(() => { store.database.close(); fs.rmSync(root, { recursive: true, force: true }); });
    for (let index = 0; index < 12; index += 1) {
        store.create({ ...imageInput(`10000000-0000-4000-8000-${String(index).padStart(12, '0')}`, null), scope: 'system', ownerUid: null, published: true, sortOrder: index });
    }
    const first = store.listSystem({ limit: 10 });
    assert.equal(first.items.length, 10);
    assert.equal(first.nextOffset, 10);
    assert.equal(store.listSystem({ offset: first.nextOffset, limit: 10 }).items.length, 2);
});

test('personal galleries are isolated and enforce ten images transactionally', t => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'hbox-gallery-'));
    const store = new ImageGalleryStore({ databasePath: path.join(root, 'gallery.sqlite3') });
    t.after(() => { store.database.close(); fs.rmSync(root, { recursive: true, force: true }); });
    for (let index = 0; index < 10; index += 1) store.create(imageInput(`00000000-0000-4000-8000-${String(index).padStart(12, '0')}`));
    assert.equal(store.listMine('user-a').count, 10);
    assert.equal(store.listMine('user-b').count, 0);
    assert.throws(() => store.create(imageInput('00000000-0000-4000-8000-999999999999')), error => error.code === 'GALLERY_LIMIT_REACHED');
    const first = store.listMine('user-a').items[0];
    store.deleteOwned([first.id], 'user-a');
    store.create(imageInput('00000000-0000-4000-8000-999999999999'));
    assert.equal(store.listMine('user-a').count, 10);
});

test('device fingerprints match only published official images or the signed-in account', t => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'hbox-gallery-match-'));
    const store = new ImageGalleryStore({ databasePath: path.join(root, 'gallery.sqlite3') });
    t.after(() => { store.close(); fs.rmSync(root, { recursive: true, force: true }); });
    const fingerprint = {
        width: 320,
        height: 172,
        frameCount: 1,
        fps: 0,
        payloadBytes: 320 * 172 * 2,
        payloadCrc32: 1,
    };
    store.create({ ...imageInput('30000000-0000-4000-8000-000000000001', null), scope: 'system', ownerUid: null, published: true });
    store.create(imageInput('30000000-0000-4000-8000-000000000002', 'user-a'));
    store.create(imageInput('30000000-0000-4000-8000-000000000003', 'user-b'));

    assert.equal(store.findByFingerprint(fingerprint).scope, 'system');
    assert.equal(store.findByFingerprint(fingerprint, 'user-a').ownerUid, 'user-a');
    assert.equal(store.findByFingerprint({ ...fingerprint, payloadCrc32: 2 }, 'user-a'), null);
});

test('gallery database stores metadata links while binaries remain in local storage', t => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'hbox-gallery-storage-'));
    const store = new ImageGalleryStore({ databasePath: path.join(root, 'gallery.sqlite3') });
    t.after(() => { store.database.close(); fs.rmSync(root, { recursive: true, force: true }); });
    const storage = new LocalGalleryStorage({ root: path.join(root, 'assets') });
    const id = '00000000-0000-4000-8000-000000000001';
    storage.putSet(id, { 'source.png': Buffer.from('source'), 'preview.png': Buffer.from('preview'), 'device.uimg': makeUimg() });
    store.create(imageInput(id));
    const columns = store.database.prepare('PRAGMA table_info(gallery_images)').all();
    assert.equal(columns.some(column => String(column.type).toUpperCase() === 'BLOB'), false);
    assert.equal(fs.readFileSync(storage.resolve(`${id}/source.png`), 'utf8'), 'source');
    assert.throws(() => storage.resolve('../outside/device.uimg'), /Invalid gallery storage key/);
});

test('gallery list routes separate device, account, and human-admin authorization', async t => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'hbox-gallery-routes-'));
    const store = new ImageGalleryStore({ databasePath: path.join(root, 'gallery.sqlite3') });
    const storage = new LocalGalleryStorage({ root: path.join(root, 'assets') });
    store.create({ ...imageInput('20000000-0000-4000-8000-000000000001', null), scope: 'system', ownerUid: null, published: true });
    const app = express();
    app.use(express.json());
    initImageGalleryRoutes(app, {
        store,
        storage,
        deviceAuth: { requireSession: scopes => (req, res, next) => req.get('Authorization') === 'Bearer device' ? (assert.deepEqual(scopes, ['config.read']), next()) : res.status(401).json({ success: false }) },
        emailAuth: {
            readSessionToken: req => String(req.get('Cookie') || '').includes('user=a') ? 'a' : null,
            resolveSession: token => token ? { uid: `user-${token}` } : null,
            requireOrigin: origin => { if (origin !== 'http://allowed') throw Object.assign(new Error('origin'), { status: 403 }); },
        },
        adminAccess: { requireAdmin: options => (req, res, next) => {
            assert.deepEqual(options, { humanOnly: true });
            return String(req.get('Cookie') || '').includes('admin=1') && !req.get('Authorization') ? next() : res.status(401).json({ success: false });
        } },
    });
    app.use((error, req, res, next) => res.status(error.status || 500).json({ success: false, error: error.code || 'ERROR' }));
    const server = app.listen(0, '127.0.0.1');
    await new Promise(resolve => server.once('listening', resolve));
    t.after(() => new Promise(resolve => server.close(resolve)));
    t.after(() => { store.close(); fs.rmSync(root, { recursive: true, force: true }); });

    assert.equal((await request(server, 'GET', '/api/gallery/system')).status, 401);
    const system = await request(server, 'GET', '/api/gallery/system', { Authorization: 'Bearer device' });
    assert.equal(system.status, 200);
    assert.equal(system.json.data.items.length, 1);
    assert.equal('deviceKey' in system.json.data.items[0], false);
    const matchQuery = '/api/gallery/match?width=320&height=172&frameCount=1&fps=0&payloadBytes=110080&payloadCrc32=1';
    assert.equal((await request(server, 'GET', matchQuery)).status, 401);
    const match = await request(server, 'GET', matchQuery, { Authorization: 'Bearer device' });
    assert.equal(match.status, 200);
    assert.equal(match.json.data.item.scope, 'system');
    assert.equal(match.json.data.item.payloadCrc32, 1);
    assert.equal((await request(
        server,
        'GET',
        matchQuery.replace('payloadBytes=110080', 'payloadBytes=1'),
        { Authorization: 'Bearer device' },
    )).status, 400);
    assert.equal((await request(server, 'GET', '/api/gallery/mine')).status, 401);
    assert.equal((await request(server, 'GET', '/api/gallery/mine', { Cookie: 'user=a' })).status, 200);
    assert.equal((await request(server, 'GET', '/api/admin/gallery/system', { Authorization: 'Bearer device' })).status, 401);
    assert.equal((await request(server, 'GET', '/api/admin/gallery/system', { Cookie: 'admin=1' })).status, 200);
});


test('6 FPS fingerprints match older assets without exposing private or unpublished images', t => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'xora-gallery-fps-'));
    const store = new ImageGalleryStore({ databasePath: path.join(root, 'gallery.sqlite3') });
    t.after(() => { store.close(); fs.rmSync(root, { recursive: true, force: true }); });
    const metadata = { frameCount: 12, fps: 3, payloadBytes: 320 * 172 * 2 * 12 };
    const privateImage = { ...imageInput('40000000-0000-4000-8000-000000000001'), ...metadata };
    store.create(privateImage);
    const fingerprint = { width: 320, height: 172, ...metadata, fps: 6, payloadCrc32: 1 };
    assert.equal(store.findByFingerprint(fingerprint, 'user-a').id, privateImage.id);
    assert.equal(store.findByFingerprint(fingerprint, 'user-b'), null);
    assert.equal(store.findByFingerprint(fingerprint), null);
    store.create({ ...imageInput('40000000-0000-4000-8000-000000000002', null), ...metadata, scope: 'system', ownerUid: null, published: false });
    assert.equal(store.findByFingerprint(fingerprint), null);
    store.create({ ...imageInput('40000000-0000-4000-8000-000000000003', null), ...metadata, scope: 'system', ownerUid: null, published: true });
    assert.equal(store.findByFingerprint(fingerprint).scope, 'system');
});

test('repeated legacy frames match after timing conversion while retaining gallery permissions', t => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'xora-gallery-timing-'));
    const store = new ImageGalleryStore({ databasePath: path.join(root, 'gallery.sqlite3') });
    t.after(() => { store.close(); fs.rmSync(root, { recursive: true, force: true }); });
    const frameSize = 320 * 172 * 2;
    for (const count of [3, 12]) {
        const bytes = makeUimg(count, 4, 3);
        for (let frame = 0; frame < count; frame++) bytes.fill(frame, 4096 + frame * frameSize, 4096 + (frame + 1) * frameSize);
        bytes.writeUInt32LE(crc32(bytes.subarray(4096)), 92);
        bytes.writeUInt32LE(crc32(bytes.subarray(0, 96)), 96);
        const id = `timing-${count}`;
        const item = { ...imageInput(id), ...parseUimg(bytes), deviceSha256: crypto.createHash('sha256').update(bytes).digest('hex') };
        store.create(item);
        const frameCount = Math.min(12, count * 2);
        const expected = Buffer.alloc(frameSize * frameCount);
        for (let frame = 0; frame < frameCount; frame++) expected.fill(Math.floor(frame / 2), frame * frameSize, (frame + 1) * frameSize);
        const fingerprint = { width: 320, height: 172, fps: 6, frameCount, payloadBytes: expected.length, payloadCrc32: crc32(expected) };
        let reads = 0;
        const read = key => { reads++; assert.equal(key, item.deviceKey); return bytes; };
        assert.equal(store.findByFingerprint(fingerprint, 'user-a', read).id, id);
        assert.equal(reads, 1);
        assert.equal(store.findByFingerprint(fingerprint, 'user-a', read).id, id);
        assert.equal(reads, 1); // Cache the derived CRC, not image bytes.
        assert.equal(store.findByFingerprint(fingerprint, 'user-b', read), null);
        assert.equal(store.findByFingerprint(fingerprint, null, read), null);
        assert.equal(reads, 1); // Private files are never read for other accounts.
        assert.equal(store.findByFingerprint({ ...fingerprint, payloadCrc32: 123 }, 'user-a', read), null);
        store.create({ ...item, id: `public-${count}`, scope: 'system', ownerUid: null, published: false });
        assert.equal(store.findByFingerprint(fingerprint, null, read), null);
        store.database.prepare('UPDATE gallery_images SET published=1 WHERE id=?').run(`public-${count}`);
        assert.equal(store.findByFingerprint(fingerprint, null, read).scope, 'system');
    }
});
