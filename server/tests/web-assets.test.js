'use strict';
const assert = require('node:assert/strict');
const test = require('node:test');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const express = require('express');
const { installWebAssetRoutes } = require('../src/web-assets');
const { createSecurityHeaders, buildContentSecurityPolicy } = require('../src/http-security');
const { installHostedWebConfig } = require('../src/hosted-webconfig');

test('versioned public assets retain old releases, allow CORS and fail closed for other files', async t => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'xora-web-assets-'));
    t.after(() => fs.rmSync(root, { recursive: true, force: true }));
    const versions = ['a'.repeat(32), 'b'.repeat(32)];
    for (const version of versions) {
        const directory = path.join(root, version);
        fs.mkdirSync(path.join(directory, 'files/_next/static'), { recursive: true });
        fs.writeFileSync(path.join(directory, 'files/_next/static/app.js'), version);
        fs.writeFileSync(path.join(directory, 'files/secret.env'), 'not published');
        fs.writeFileSync(path.join(directory, 'manifest.json'), JSON.stringify({
            schema: 1, version, files: { '_next/static/app.js': 'hash', 'fonts/missing.woff2': 'hash' },
        }));
    }
    const app = express();
    app.use(createSecurityHeaders());
    installWebAssetRoutes(app, root);
    const server = app.listen(0, '127.0.0.1');
    await new Promise(resolve => server.once('listening', resolve));
    t.after(() => new Promise(resolve => { server.close(resolve); server.closeAllConnections(); }));
    const base = `http://127.0.0.1:${server.address().port}`;
    for (const version of versions) {
        const response = await fetch(`${base}/cdn/web/${version}/_next/static/app.js`);
        assert.equal(response.status, 200);
        assert.equal(await response.text(), version);
        assert.equal(response.headers.get('access-control-allow-origin'), '*');
        assert.equal(response.headers.get('access-control-allow-credentials'), null);
        assert.equal(response.headers.get('cross-origin-resource-policy'), 'cross-origin');
        assert.equal(response.headers.get('timing-allow-origin'), '*');
        assert.equal(response.headers.get('cache-control'), 'public, max-age=31536000, immutable');
        assert.equal((await fetch(response.url, { method: 'HEAD' })).status, 200);
    }
    for (const relative of ['secret.env', 'index.html', 'manifest.json', 'fonts/missing.woff2', '_next/static/missing.js']) {
        const response = await fetch(`${base}/cdn/web/${versions[0]}/${relative}`);
        assert.equal(response.status, 404, relative);
        assert.equal(response.headers.get('cache-control'), 'no-store', relative);
    }
    for (const url of ['/cdn/web/not-a-version/_next/static/app.js', '/cdn/web/' + 'c'.repeat(32) + '/_next/static/app.js', '/cdn/web/']) {
        const response = await fetch(base + url);
        assert.equal(response.status, 404);
        assert.equal(response.headers.get('cache-control'), 'no-store');
    }
});

test('CSP adds only the configured CDN origin without weakening script execution rules', () => {
    const origin = 'https://assets.example.com';
    const policy = buildContentSecurityPolicy({ webCdnOrigin: origin, galleryCdnOrigin: origin });
    for (const directive of ['script-src', 'style-src', 'img-src', 'font-src', 'connect-src']) {
        assert.ok(policy.split('; ').find(value => value.startsWith(directive + ' ')).includes(origin));
    }
    assert.doesNotMatch(policy, /script-src[^;]*(?:unsafe-inline|unsafe-eval|\*)/);
    assert.equal(policy.split('; ').find(value => value.startsWith('connect-src')), `connect-src 'self' ${origin}`);
    assert.throws(() => buildContentSecurityPolicy({ webCdnOrigin: 'https://assets.example.com/path' }));
});

test('existing same-origin chunk URLs survive the first CDN migration', async t => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'xora-legacy-assets-'));
    t.after(() => fs.rmSync(root, { recursive: true, force: true }));
    for (const directory of ['current/_next/static', 'assets/legacy/_next/static']) {
        fs.mkdirSync(path.join(root, directory), { recursive: true });
    }
    fs.writeFileSync(path.join(root, 'current/index.html'), '<html>current</html>');
    fs.writeFileSync(path.join(root, 'current/_next/static/new.js'), 'new');
    fs.writeFileSync(path.join(root, 'assets/legacy/_next/static/old.js'), 'old');
    const app = express();
    installHostedWebConfig(app, { staticDir: path.join(root, 'current'), legacyAssetDir: path.join(root, 'assets'),
        webCdnOrigin: 'https://assets.example.com' });
    const server = app.listen(0, '127.0.0.1');
    await new Promise(resolve => server.once('listening', resolve));
    t.after(() => new Promise(resolve => { server.close(resolve); server.closeAllConnections(); }));
    const page = await fetch(`http://127.0.0.1:${server.address().port}/`);
    assert.match(page.headers.get('link'), /<https:\/\/assets.example.com>; rel=preconnect; crossorigin/);
    assert.match(page.headers.get('link'), /rel=dns-prefetch/);
    assert.equal(page.headers.get('alt-svc'), null);
    for (const name of ['new', 'old']) {
        const response = await fetch(`http://127.0.0.1:${server.address().port}/_next/static/${name}.js`);
        assert.equal(response.status, 200);
        assert.equal(await response.text(), name);
        assert.match(response.headers.get('cache-control'), /immutable/);
    }
});
