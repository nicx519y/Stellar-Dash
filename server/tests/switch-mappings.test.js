'use strict';

const assert = require('node:assert/strict');
const fs = require('fs-extra');
const http = require('node:http');
const os = require('node:os');
const path = require('node:path');
const test = require('node:test');
const express = require('express');

const {
    SwitchMappingStore,
    initSwitchMappingRoutes,
    mappingSha256,
} = require('../src/switch-mappings');

const COMPATIBILITY = {
    productId: 'HBOX-V2',
    pcbRevision: 'PCB-2026-08',
    hardwareVersion: '2.1.0',
};

function mapping(name = 'Axis A', offset = 0) {
    return {
        name,
        length: 4,
        step: 0.5,
        samplingNoise: 3,
        samplingFrequency: 8000,
        originalValues: [4000 + offset, 3000 + offset, 2000 + offset, 900 + offset],
    };
}

function request(server, method, requestPath, options = {}) {
    return new Promise((resolve, reject) => {
        const body = options.rawBody !== undefined
            ? Buffer.from(options.rawBody)
            : options.body === undefined ? null : Buffer.from(JSON.stringify(options.body));
        const address = server.address();
        const req = http.request({
            host: '127.0.0.1', port: address.port, method, path: requestPath,
            headers: {
                ...(body ? {
                    'Content-Type': options.rawBody !== undefined
                        ? options.contentType
                        : 'application/json',
                    'Content-Length': body.length,
                } : {}),
                ...options.headers,
            },
        }, response => {
            const chunks = [];
            response.on('data', chunk => chunks.push(chunk));
            response.on('end', () => {
                const responseBody = Buffer.concat(chunks);
                const contentType = String(response.headers['content-type'] || '');
                resolve({
                    status: response.statusCode,
                    headers: response.headers,
                    body: responseBody,
                    json: contentType.includes('application/json')
                        ? JSON.parse(responseBody.toString('utf8'))
                        : null,
                });
            });
        });
        req.on('error', reject);
        if (body) req.write(body);
        req.end();
    });
}

function fixture(t) {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'hbox-switch-mappings-'));
    const store = new SwitchMappingStore({
        databasePath: path.join(root, 'switch_mappings.sqlite3'),
        now: () => '2026-08-30T00:00:00.000Z',
    });
    const deviceAuth = {
        requireSession(requiredScopes) {
            return (req, res, next) => {
                if (req.get('Authorization') !== 'Bearer device-read') {
                    return res.status(401).json({ success: false, error: 'DEVICE_AUTH_REQUIRED' });
                }
                assert.deepEqual(requiredScopes, ['config.read']);
                req.deviceSession = { ...COMPATIBILITY };
                next();
            };
        },
    };
    const adminAccess = {
        requireAdmin(options) {
            return (req, res, next) => {
                if (!String(req.get('Cookie') || '').includes('admin=1')) {
                    return res.status(401).json({ success: false, error: 'ADMIN_AUTH_REQUIRED' });
                }
                if (req.get('Authorization') && !options.allowDeviceBearer) {
                    return res.status(403).json({ success: false, error: 'HUMAN_ADMIN_REQUIRED' });
                }
                req.authenticatedAdmin = { username: 'admin@example.com' };
                next();
            };
        },
    };
    const app = express();
    app.use(express.json());
    initSwitchMappingRoutes(app, { store, deviceAuth, adminAccess });
    const server = app.listen(0, '127.0.0.1');
    t.after(() => new Promise(resolve => server.close(resolve)));
    t.after(() => {
        store.close();
        fs.removeSync(root);
    });
    return { store, server };
}

test('published revisions are immutable, compatible and hidden until publish', async t => {
    const { store } = fixture(t);
    const first = store.createDraft({
        catalogId: null,
        metadata: { displayName: 'Linear Silver', description: 'public curve' },
        compatibility: COMPATIBILITY,
        mapping: mapping(),
        actor: 'admin@example.com',
    });
    assert.equal(store.listPublished(COMPATIBILITY).length, 0);
    assert.equal(first.revision.revisionId.length <= 15, true);
    assert.equal(first.revision.sha256, mappingSha256(first.revision.mapping));

    store.publish(first.catalogId, first.revision.revisionId);
    assert.equal(store.listPublished(COMPATIBILITY).length, 1);
    assert.equal(store.listPublished({ ...COMPATIBILITY, pcbRevision: 'other' }).length, 0);

    const second = store.createDraft({
        catalogId: first.catalogId,
        metadata: { displayName: 'Linear Silver', description: 'revision two' },
        compatibility: COMPATIBILITY,
        mapping: mapping('Axis A2', -20),
        actor: 'admin@example.com',
    });
    assert.equal(second.revision.revision, 2);
    assert.notEqual(second.revision.revisionId, first.revision.revisionId);
    assert.deepEqual(store.revision(first.revision.revisionId).original_values, '[4000,3000,2000,900]');
    assert.equal(store.listPublished(COMPATIBILITY)[0].revisionId, first.revision.revisionId);

    store.publish(first.catalogId, second.revision.revisionId);
    store.unpublish(first.catalogId);
    assert.equal(store.listPublished(COMPATIBILITY).length, 0);
    assert.ok(store.revision(first.revision.revisionId));
    assert.ok(store.revision(second.revision.revisionId));
});

test('routes require device read plus administrator cookie for draft publication', async t => {
    const { server } = fixture(t);
    await new Promise(resolve => server.once('listening', resolve));
    const body = {
        displayName: 'Tactile Blue',
        description: 'test revision',
        mapping: mapping('Blue Axis'),
    };
    assert.equal((await request(server, 'GET', '/api/switch-mappings')).status, 401);
    assert.equal((await request(server, 'POST', '/api/admin/switch-mappings/drafts', {
        headers: { Authorization: 'Bearer device-read' }, body,
    })).status, 401);

    const created = await request(server, 'POST', '/api/admin/switch-mappings/drafts', {
        headers: { Authorization: 'Bearer device-read', Cookie: 'admin=1' }, body,
    });
    assert.equal(created.status, 201);
    const draft = created.json.data;
    const hidden = await request(server, 'GET', '/api/switch-mappings', {
        headers: { Authorization: 'Bearer device-read' },
    });
    assert.deepEqual(hidden.json.data.items, []);

    const published = await request(
        server,
        'POST',
        `/api/admin/switch-mappings/${draft.catalogId}/revisions/${draft.revision.revisionId}/publish`,
        { headers: { Authorization: 'Bearer device-read', Cookie: 'admin=1' } }
    );
    assert.equal(published.status, 200);
    const list = await request(server, 'GET', '/api/switch-mappings', {
        headers: { Authorization: 'Bearer device-read' },
    });
    assert.equal(list.json.data.items[0].revisionId, draft.revision.revisionId);
    const detail = await request(server, 'GET', `/api/switch-mappings/${draft.catalogId}`, {
        headers: { Authorization: 'Bearer device-read' },
    });
    assert.equal(detail.json.data.revision.mapping.id, draft.revision.revisionId);
    assert.equal(detail.json.data.revision.sha256, mappingSha256(detail.json.data.revision.mapping));
});

test('administrator creates, publishes, and updates a normal zero-filled mapping', async t => {
    const { server } = fixture(t);
    await new Promise(resolve => server.once('listening', resolve));
    const headers = { Authorization: 'Bearer device-read', Cookie: 'admin=1' };
    const created = await request(
        server,
        'POST',
        '/api/admin/switch-mappings/blank',
        {
            headers,
            body: {
                displayName: 'Blank Axis',
                description: '',
                length: 12,
                step: 0.2,
            },
        }
    );
    assert.equal(created.status, 201);
    assert.equal(
        created.json.data.publishedRevisionId,
        created.json.data.revision.revisionId
    );
    assert.equal(created.json.data.revision.mapping.length, 12);
    assert.equal(created.json.data.revision.mapping.step, Math.fround(0.2));
    assert.deepEqual(created.json.data.revision.mapping.originalValues, Array(12).fill(0));

    const publicList = await request(server, 'GET', '/api/switch-mappings', {
        headers: { Authorization: 'Bearer device-read' },
    });
    assert.equal(publicList.json.data.items.length, 1);
    assert.equal(publicList.json.data.items[0].displayName, 'Blank Axis');

    const adminList = await request(
        server,
        'GET',
        '/api/admin/switch-mappings-compatible',
        { headers }
    );
    assert.equal(adminList.status, 200);
    assert.equal(adminList.json.data.items[0].displayName, 'Blank Axis');
    assert.equal(adminList.json.data.items[0].isDraft, false);

    const detail = await request(
        server,
        'GET',
        `/api/switch-mappings/${created.json.data.catalogId}`,
        { headers: { Authorization: 'Bearer device-read' } }
    );
    assert.deepEqual(detail.json.data.revision.mapping.originalValues, Array(12).fill(0));

    const recordedMapping = {
        ...created.json.data.revision.mapping,
        samplingNoise: 7,
        samplingFrequency: 8100,
        originalValues: [4000, 3700, ...Array(10).fill(0)],
    };
    const updated = await request(
        server,
        'PATCH',
        `/api/admin/switch-mappings/${created.json.data.catalogId}/mapping`,
        { headers, body: { mapping: recordedMapping } }
    );
    assert.equal(updated.status, 200);
    assert.equal(updated.json.data.revision.revisionId, recordedMapping.id);
    assert.deepEqual(
        updated.json.data.revision.mapping.originalValues,
        recordedMapping.originalValues
    );
    assert.equal(
        updated.json.data.revision.sha256,
        mappingSha256(updated.json.data.revision.mapping)
    );
});

test('server startup promotes a legacy hidden mapping to the normal catalog', t => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'hbox-switch-migration-'));
    const databasePath = path.join(root, 'switch_mappings.sqlite3');
    const oldStore = new SwitchMappingStore({
        databasePath,
        now: () => '2026-08-29T00:00:00.000Z',
    });
    const hidden = oldStore.createDraft({
        catalogId: null,
        metadata: { displayName: 'Legacy Blank', description: '' },
        compatibility: COMPATIBILITY,
        mapping: {
            name: 'Legacy Blank',
            length: 4,
            step: 0.5,
            samplingNoise: 0,
            samplingFrequency: 1,
            originalValues: [0, 0, 0, 0],
        },
        actor: 'admin@example.com',
        allowBlank: true,
    });
    oldStore.close();

    const migratedStore = new SwitchMappingStore({
        databasePath,
        now: () => '2026-08-30T00:00:00.000Z',
    });
    t.after(() => {
        migratedStore.close();
        fs.removeSync(root);
    });
    const migrated = migratedStore.published(hidden.catalogId, COMPATIBILITY);
    assert.equal(migrated.publishedRevisionId, hidden.revision.revisionId);
    assert.deepEqual(migrated.revision.mapping.originalValues, [0, 0, 0, 0]);
});

test('mapping validation rejects invalid samples and cross-hardware revisions', t => {
    const { store } = fixture(t);
    assert.throws(() => store.createDraft({
        catalogId: null,
        metadata: { displayName: 'Bad', description: '' },
        compatibility: COMPATIBILITY,
        mapping: { ...mapping(), originalValues: [1000, 1000, 1000, 1000] },
        actor: 'admin@example.com',
    }), /endpoints must be different/);
    const first = store.createDraft({
        catalogId: null,
        metadata: { displayName: 'Good', description: '' },
        compatibility: COMPATIBILITY,
        mapping: mapping(),
        actor: 'admin@example.com',
    });
    assert.throws(() => store.createDraft({
        catalogId: first.catalogId,
        metadata: { displayName: 'Good', description: '' },
        compatibility: { ...COMPATIBILITY, hardwareVersion: '9.9.9' },
        mapping: mapping('Other'),
        actor: 'admin@example.com',
    }), /different hardware/);
});

test('catalog names are unique per compatible hardware for create and rename', t => {
    const { store } = fixture(t);
    const first = store.createDraft({
        catalogId: null,
        metadata: { displayName: '  Linear Silver  ', description: '' },
        compatibility: COMPATIBILITY,
        mapping: mapping(),
        actor: 'admin@example.com',
    });

    assert.throws(() => store.createDraft({
        catalogId: null,
        metadata: { displayName: 'linear silver', description: '' },
        compatibility: COMPATIBILITY,
        mapping: mapping('Duplicate'),
        actor: 'admin@example.com',
    }), error => error.code === 'SWITCH_MAPPING_NAME_CONFLICT' && error.status === 409);

    const second = store.createDraft({
        catalogId: null,
        metadata: { displayName: 'Tactile Blue', description: '' },
        compatibility: COMPATIBILITY,
        mapping: mapping('Blue Axis'),
        actor: 'admin@example.com',
    });
    assert.throws(
        () => store.updateMetadata(second.catalogId, {
            displayName: 'LINEAR SILVER',
            description: '',
        }),
        error => error.code === 'SWITCH_MAPPING_NAME_CONFLICT' && error.status === 409
    );

    const revision = store.createDraft({
        catalogId: first.catalogId,
        metadata: { displayName: 'Linear Silver', description: 'revision two' },
        compatibility: COMPATIBILITY,
        mapping: mapping('Revision 2', -10),
        actor: 'admin@example.com',
    });
    assert.equal(revision.revision.revision, 2);

    const otherHardware = store.createDraft({
        catalogId: null,
        metadata: { displayName: 'Linear Silver', description: '' },
        compatibility: { ...COMPATIBILITY, hardwareVersion: '2.2.0' },
        mapping: mapping('Other HW'),
        actor: 'admin@example.com',
    });
    assert.ok(otherHardware.catalogId);
});

test('canonical digest accepts the maximum 40-point curve', t => {
    const { store } = fixture(t);
    const originalValues = Array.from({ length: 40 }, (_, index) => 4000 - index * 50);
    const draft = store.createDraft({
        catalogId: null,
        metadata: { displayName: 'Long Curve', description: '' },
        compatibility: COMPATIBILITY,
        mapping: {
            name: 'Long', length: 40, step: 0.1,
            samplingNoise: 1, samplingFrequency: 8000, originalValues,
        },
        actor: 'admin@example.com',
    });
    assert.equal(draft.revision.mapping.originalValues.length, 40);
    assert.equal(draft.revision.sha256, mappingSha256(draft.revision.mapping));
});

test('administrator can upload an image and physically delete a published mapping', async t => {
    const { store, server } = fixture(t);
    await new Promise(resolve => server.once('listening', resolve));
    const draft = store.createDraft({
        catalogId: null,
        metadata: { displayName: 'Image Axis', description: '' },
        compatibility: COMPATIBILITY,
        mapping: mapping('Image Axis'),
        actor: 'admin@example.com',
    });
    store.publish(draft.catalogId, draft.revision.revisionId, 'admin@example.com');

    const png = Buffer.concat([
        Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
        Buffer.alloc(16, 0),
    ]);
    const deniedUpload = await request(
        server,
        'PUT',
        `/api/admin/switch-mappings/${draft.catalogId}/image`,
        {
            headers: { Authorization: 'Bearer device-read' },
            rawBody: png,
            contentType: 'image/png',
        }
    );
    assert.equal(deniedUpload.status, 401);
    const uploaded = await request(
        server,
        'PUT',
        `/api/admin/switch-mappings/${draft.catalogId}/image`,
        {
            headers: {
                Authorization: 'Bearer device-read',
                Cookie: 'admin=1',
            },
            rawBody: png,
            contentType: 'image/png',
        }
    );
    assert.equal(uploaded.status, 200);
    assert.equal(uploaded.json.data.hasImage, true);

    const renamed = await request(
        server,
        'PATCH',
        `/api/admin/switch-mappings/${draft.catalogId}`,
        {
            headers: {
                Authorization: 'Bearer device-read',
                Cookie: 'admin=1',
            },
            body: { displayName: 'Renamed Axis', description: 'edited' },
        }
    );
    assert.equal(renamed.status, 200);
    assert.equal(renamed.json.data.displayName, 'Renamed Axis');

    const list = await request(server, 'GET', '/api/switch-mappings', {
        headers: { Authorization: 'Bearer device-read' },
    });
    assert.equal(list.json.data.items[0].displayName, 'Renamed Axis');
    assert.equal(list.json.data.items[0].hasImage, true);
    assert.equal(list.json.data.items[0].imageUpdatedAt, '2026-08-30T00:00:00.000Z');

    const image = await request(
        server,
        'GET',
        `/api/switch-mappings/${draft.catalogId}/image`,
        { headers: { Authorization: 'Bearer device-read' } }
    );
    assert.equal(image.status, 200);
    assert.equal(image.headers['content-type'], 'image/png');
    assert.deepEqual(image.body, png);

    const deniedDelete = await request(
        server,
        'DELETE',
        `/api/admin/switch-mappings/${draft.catalogId}`,
        { headers: { Authorization: 'Bearer device-read' } }
    );
    assert.equal(deniedDelete.status, 401);

    const deleted = await request(
        server,
        'DELETE',
        `/api/admin/switch-mappings/${draft.catalogId}`,
        {
            headers: {
                Authorization: 'Bearer device-read',
                Cookie: 'admin=1',
            },
        }
    );
    assert.equal(deleted.status, 200);
    assert.equal(deleted.json.data.deleted, true);
    assert.deepEqual(deleted.json.data.deletedRevisionIds, [draft.revision.revisionId]);
    const after = await request(server, 'GET', '/api/switch-mappings', {
        headers: { Authorization: 'Bearer device-read' },
    });
    assert.deepEqual(after.json.data.items, []);
    assert.equal(store.catalog(draft.catalogId), null);
    assert.equal(store.revision(draft.revision.revisionId), null);
});
