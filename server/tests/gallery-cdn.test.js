'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const http = require('node:http');
const express = require('express');
const cors = require('cors');
const { ImageGalleryStore, LocalGalleryStorage, initImageGalleryRoutes } = require('../src/image-gallery');
const { parseGalleryCdnOrigin, galleryAssetUrls, initPublicGalleryAssetRoutes } = require('../src/gallery-cdn');
const { createSecurityHeaders, createExactCorsOptions } = require('../src/http-security');
const { installHostedWebConfig } = require('../src/hosted-webconfig');

const CDN = 'https://assets.example.com';
const SITE = 'https://config.example.com';

function request(server, url, headers = {}, method = 'GET') {
    return new Promise((resolve, reject) => {
        const req = http.request({ hostname: '127.0.0.1', port: server.address().port, path: url, headers, method }, res => {
            const chunks = [];
            res.on('data', chunk => chunks.push(chunk));
            res.on('end', () => resolve({ status: res.statusCode, headers: res.headers, body: Buffer.concat(chunks) }));
        });
        req.on('error', reject);
        req.end();
    });
}

test('CDN origin is optional and rejects paths, credentials and CSP injection', () => {
    assert.equal(parseGalleryCdnOrigin(undefined), '');
    assert.equal(parseGalleryCdnOrigin(''), '');
    assert.equal(parseGalleryCdnOrigin(CDN), CDN);
    for (const value of [null, false, 'http://assets.example.com', CDN + '/', CDN + '/path',
        CDN + '?x=1', CDN + '#fragment', CDN + ':443', 'https://user@assets.example.com',
        'https://assets.example.com; connect-src *', 'https://a..example.com', 'https://-bad.example.com']) {
        assert.throws(() => parseGalleryCdnOrigin(value));
    }
});

test('only published official images receive CDN URLs', () => {
    const item = { id: 'image-id', scope: 'system', published: true, updatedAt: 42, deviceSha256: 'a'.repeat(64) };
    assert.equal(galleryAssetUrls(item, CDN).previewUrl, CDN + '/cdn/gallery/image-id/16-aaaaaaaaaaaaaaaa/preview');
    for (const value of [{ ...item, scope: 'user' }, { ...item, published: false }]) {
        assert.equal(galleryAssetUrls(value, CDN).previewUrl, '/api/gallery/images/image-id/preview');
    }
    assert.equal(galleryAssetUrls(item).previewUrl, '/api/gallery/images/image-id/preview');
});

test('public origin isolates private images, drafts, old versions and uncached errors', async t => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'xora-cdn-'));
    const store = new ImageGalleryStore({ databasePath: path.join(root, 'gallery.sqlite3') });
    const storage = new LocalGalleryStorage({ root: path.join(root, 'assets') });
    t.after(() => { store.close(); fs.rmSync(root, { recursive: true, force: true }); });
    const items = ['system', 'user', 'draft', 'missing'].map((kind, index) => {
        const id = `50000000-0000-4000-8000-${String(index).padStart(12, '0')}`;
        if (kind !== 'missing') storage.putSet(id, {
            'source.gif': Buffer.from('original-gif'), 'preview.png': Buffer.from('poster-frame-three'), 'device.uimg': Buffer.from('device-asset'),
        });
        return store.create({
            id, scope: kind === 'user' ? 'user' : 'system', ownerUid: kind === 'user' ? 'owner' : null,
            title: kind, sourceKey: `${id}/source.gif`, previewKey: `${id}/preview.png`, deviceKey: `${id}/device.uimg`,
            sourceMime: 'image/gif', width: 320, height: 172, frameCount: 1, fps: 0,
            payloadBytes: 110080, payloadCrc32: index + 1, deviceSha256: 'a'.repeat(64), published: kind !== 'draft',
        });
    });
    const app = express();
    app.use(createSecurityHeaders({ galleryCdnOrigin: CDN }));
    app.use(cors(createExactCorsOptions([SITE])));
    app.use(express.json());
    initImageGalleryRoutes(app, {
        store, storage, cdnOrigin: CDN,
        deviceAuth: { requireSession: () => (req, res, next) => req.get('Authorization') === 'Bearer device' ? next() : res.status(401).end() },
        emailAuth: { readSessionToken: req => req.get('Cookie') === 'user=owner' ? 'owner' : null,
            resolveSession: token => token ? { uid: token } : null, requireOrigin() {} },
        adminAccess: { requireAdmin: () => (req, res, next) => req.get('Cookie') === 'admin=1' ? next() : res.status(401).end() },
    });
    initPublicGalleryAssetRoutes(app, { store, storage });
    fs.mkdirSync(path.join(root, '_next/static'), { recursive: true });
    fs.mkdirSync(path.join(root, 'global'), { recursive: true });
    fs.writeFileSync(path.join(root, 'index.html'), '<script>window.boot=1</script>');
    fs.writeFileSync(path.join(root, 'global/index.html'), '<script>window.boot=2</script>');
    installHostedWebConfig(app, { staticDir: root, required: true, galleryCdnOrigin: CDN });
    app.use((err, req, res, next) => res.status(err.status || 500).json({ error: err.code }));
    app.use((req, res) => res.status(404).end());
    const server = app.listen(0, '127.0.0.1');
    await new Promise(resolve => server.once('listening', resolve));
    t.after(() => new Promise(resolve => server.close(resolve)));
    const publicPath = (item, variant = 'preview') => new URL(galleryAssetUrls({ ...item, scope: 'system', published: true }, CDN)[
        variant === 'source' ? 'sourceUrl' : variant === 'device' ? 'deviceAssetUrl' : 'previewUrl'
    ]).pathname;

    const list = await request(server, '/api/gallery/system', { Authorization: 'Bearer device' });
    assert.equal(list.status, 200);
    assert.equal(list.headers['cache-control'], 'private, no-store');
    assert.equal(JSON.parse(list.body).data.items.find(item => item.id === items[0].id).previewUrl,
        CDN + publicPath(items[0]));
    const mine = await request(server, '/api/gallery/mine', { Cookie: 'user=owner' });
    assert.equal(JSON.parse(mine.body).data.items[0].sourceUrl, `/api/gallery/images/${items[1].id}/source`);
    const admin = await request(server, '/api/admin/gallery/system', { Cookie: 'admin=1' });
    assert.ok(JSON.parse(admin.body).data.items.every(item => item.previewUrl.startsWith('/api/admin/')));
    const match = await request(server, '/api/gallery/match?width=320&height=172&frameCount=1&fps=0&payloadBytes=110080&payloadCrc32=1', { Authorization: 'Bearer device' });
    assert.equal(JSON.parse(match.body).data.item.previewUrl, CDN + publicPath(items[0]));

    for (const [variant, expected] of [['preview', 'poster-frame-three'], ['source', 'original-gif'], ['device', 'device-asset']]) {
        const asset = await request(server, publicPath(items[0], variant), { Origin: SITE });
        assert.equal(asset.status, 200);
        assert.equal(asset.body.toString(), expected);
        assert.equal(asset.headers['cache-control'], 'public, max-age=300');
        assert.equal(asset.headers['access-control-allow-origin'], '*');
        assert.equal(asset.headers['access-control-allow-credentials'], undefined);
        assert.equal(asset.headers['cross-origin-resource-policy'], 'cross-origin');
    }
    const head = await request(server, publicPath(items[0]), {}, 'HEAD');
    assert.equal(head.status, 200);
    assert.equal(head.body.length, 0);
    assert.equal((await request(server, `/api/gallery/images/${items[1].id}/preview`)).status, 401);
    assert.equal((await request(server, `/api/gallery/images/${items[2].id}/preview`, { Authorization: 'Bearer device' })).status, 404);
    for (const url of [publicPath(items[1]), publicPath(items[2]), publicPath(items[3]),
        publicPath(items[0]).replace('/preview', '/invalid'), publicPath(items[0]).replace('/preview', ''),
        publicPath(items[0]).replace(/\/[a-z0-9]+-[a-f0-9]{16}\//, '/old-version/'), '/cdn/gallery/unknown']) {
        const error = await request(server, url);
        assert.equal(error.status, 404, url);
        assert.equal(error.headers['cache-control'], 'no-store', url);
    }
    // Listing and direct HTML responses must both allow only this CDN for fetch.
    for (const url of ['/api/gallery/system', '/', '/global/', '/keys']) {
        const response = await request(server, url, { Authorization: 'Bearer device' });
        const policy = response.headers['content-security-policy'];
        assert.ok(policy.includes(`connect-src 'self' ${CDN}`));
        assert.ok(!policy.split('; ').find(value => value.startsWith('script-src ')).includes(CDN));
        if (url !== '/api/gallery/system') assert.match(policy, /script-src 'self' 'sha256-/);
    }
    store.updateSystem(items[0].id, { published: false });
    const withdrawn = await request(server, publicPath(items[0]));
    assert.equal(withdrawn.status, 404);
    assert.equal(withdrawn.headers['cache-control'], 'no-store');
});
