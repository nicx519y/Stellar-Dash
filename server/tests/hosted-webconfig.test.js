'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const http = require('node:http');
const os = require('node:os');
const path = require('node:path');
const test = require('node:test');

const express = require('express');
const {
    inlineScriptHashes,
    installHostedWebConfig,
    resolveHostedWebConfigOptions
} = require('../src/hosted-webconfig');
const {
    parseTrustedProxyHops,
    securityHeaders
} = require('../src/http-security');
const { resolveServerStoragePaths } = require('../src/server-paths');

function request(server, requestPath) {
    return new Promise((resolve, reject) => {
        const address = server.address();
        const request = http.get({
            host: '127.0.0.1',
            port: address.port,
            path: requestPath
        }, response => {
            const chunks = [];
            response.on('data', chunk => chunks.push(chunk));
            response.on('end', () => resolve({
                status: response.statusCode,
                headers: response.headers,
                body: Buffer.concat(chunks).toString('utf8')
            }));
        });
        request.on('error', reject);
    });
}

test('hosted export serves HTML and immutable Next assets', async t => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'hbox-webconfig-'));
    t.after(() => fs.rmSync(root, { recursive: true, force: true }));
    fs.mkdirSync(path.join(root, '_next', 'static'), { recursive: true });
    fs.writeFileSync(
        path.join(root, 'index.html'),
        '<!doctype html><script>globalThis.__NEXT_BOOT=1;</script>HBox'
    );
    fs.writeFileSync(
        path.join(root, '_next', 'static', 'app.123.js'),
        'globalThis.HBOX=true;'
    );

    const app = express();
    app.use(securityHeaders);
    app.get('/api/status', (req, res) => res.json({ api: true }));
    const state = installHostedWebConfig(app, {
        staticDir: root,
        required: true,
        profileSlugs: ['hbox-pcb-v2']
    });
    app.use((req, res) => res.status(404).json({ missing: req.path }));
    const server = app.listen(0);
    t.after(() => new Promise(resolve => server.close(resolve)));

    assert.equal(state.enabled, true);
    const page = await request(server, '/');
    assert.equal(page.status, 200);
    assert.match(page.body, /HBox/);
    assert.equal(page.headers['cache-control'], 'no-cache, no-store');
    assert.equal(
        page.headers['cross-origin-opener-policy'],
        'same-origin'
    );
    const [expectedHash] = inlineScriptHashes(
        '<script>globalThis.__NEXT_BOOT=1;</script>'
    );
    assert.match(
        page.headers['content-security-policy'],
        new RegExp(expectedHash.replace(/[+]/g, '\\+'))
    );
    assert.doesNotMatch(
        page.headers['content-security-policy'],
        /script-src[^;]*unsafe-inline/
    );

    const asset = await request(server, '/_next/static/app.123.js');
    assert.equal(asset.status, 200);
    assert.match(asset.headers['cache-control'], /immutable/);

    const routeFallback = await request(server, '/global');
    assert.equal(routeFallback.status, 200);
    assert.match(routeFallback.body, /HBox/);
    assert.equal(
        routeFallback.headers['cache-control'],
        'no-cache, no-store'
    );

    const profiledRoute = await request(
        server,
        '/webconfig/hbox-pcb-v2/keys/'
    );
    assert.equal(profiledRoute.status, 200);
    assert.match(profiledRoute.body, /HBox/);

    const invalidProfileRoute = await request(
        server,
        '/webconfig/INVALID/keys/'
    );
    assert.equal(invalidProfileRoute.status, 404);

    const unknownValidProfileRoute = await request(
        server,
        '/webconfig/evil/keys/'
    );
    assert.equal(unknownValidProfileRoute.status, 404);

    const api = await request(server, '/api/status');
    assert.equal(api.status, 200);
    assert.deepEqual(JSON.parse(api.body), { api: true });

    const unknownApi = await request(server, '/api/not-found');
    assert.equal(unknownApi.status, 404);
    assert.deepEqual(
        JSON.parse(unknownApi.body),
        { missing: '/api/not-found' }
    );

    const unknownPage = await request(server, '/not-a-config-route');
    assert.equal(unknownPage.status, 404);
    assert.deepEqual(
        JSON.parse(unknownPage.body),
        { missing: '/not-a-config-route' }
    );
});

test('required hosted export fails closed when index is absent', () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'hbox-webconfig-'));
    try {
        assert.throws(
            () => installHostedWebConfig(express(), {
                staticDir: root,
                required: true
            }),
            /index is missing/
        );
    } finally {
        fs.rmSync(root, { recursive: true, force: true });
    }
});

test('required hosted export fails closed when Next assets are absent', () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'hbox-webconfig-'));
    try {
        fs.writeFileSync(path.join(root, 'index.html'), '<!doctype html>HBox');
        assert.throws(
            () => installHostedWebConfig(express(), {
                staticDir: root,
                required: true
            }),
            /_next[\\/]static/
        );
    } finally {
        fs.rmSync(root, { recursive: true, force: true });
    }
});

test('deployment environment resolves an explicit static directory', () => {
    const root = path.resolve('deployed-webconfig');
    const options = resolveHostedWebConfigOptions({
        WEB_CONFIG_STATIC_DIR: root,
        WEB_CONFIG_REQUIRE_STATIC: 'true'
    }, path.resolve('server'));
    assert.deepEqual(options, {
        staticDir: root,
        required: true
    });
});

test('trusted proxy hop count is explicit and narrowly bounded', () => {
    assert.equal(parseTrustedProxyHops(undefined), 0);
    assert.equal(parseTrustedProxyHops('1'), 1);
    assert.throws(() => parseTrustedProxyHops('true'), /must be 0, 1, or 2/);
    assert.throws(() => parseTrustedProxyHops('3'), /must be 0, 1, or 2/);
});

test('server state and uploads can be isolated outside the repository', t => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'hbox-server-state-'));
    t.after(() => fs.rmSync(root, { recursive: true, force: true }));
    const dataDir = path.join(root, 'isolated-data');
    const uploadDir = path.join(root, 'isolated-uploads');
    const environment = {
        NODE_ENV: 'development',
        HBOX_SERVER_DATA_DIR: dataDir,
        HBOX_SERVER_UPLOAD_DIR: uploadDir
    };
    const resolved = resolveServerStoragePaths(environment, root);
    assert.equal(resolved.dataDir, dataDir);
    assert.equal(resolved.uploadDir, uploadDir);
    assert.equal(resolved.deviceDataFile, path.join(dataDir, 'device_ids.json'));
    assert.equal(
        resolved.accountDatabase,
        path.join(dataDir, 'accounts.sqlite3')
    );
    assert.equal(
        resolved.userAccountDatabase,
        path.join(dataDir, 'user_accounts.sqlite3')
    );

    assert.equal(resolved.authConfigFile, undefined);
    assert.equal(fs.existsSync(path.join(dataDir, 'auth_config.json')), false);
});

test('production storage directories are explicit absolute paths', () => {
    const root = path.resolve('server-fixture');
    assert.throws(
        () => resolveServerStoragePaths({ NODE_ENV: 'production' }, root),
        /HBOX_SERVER_DATA_DIR is required/
    );
    assert.throws(
        () => resolveServerStoragePaths({
            NODE_ENV: 'production',
            HBOX_SERVER_DATA_DIR: 'relative-data',
            HBOX_SERVER_UPLOAD_DIR: path.join(root, 'uploads')
        }, root),
        /HBOX_SERVER_DATA_DIR must be an absolute path/
    );
    const resolved = resolveServerStoragePaths({
        NODE_ENV: 'production',
        HBOX_SERVER_DATA_DIR: path.join(root, 'data'),
        HBOX_SERVER_UPLOAD_DIR: path.join(root, 'uploads')
    }, root);
    assert.equal(resolved.dataDir, path.join(root, 'data'));
    assert.equal(resolved.uploadDir, path.join(root, 'uploads'));
});
