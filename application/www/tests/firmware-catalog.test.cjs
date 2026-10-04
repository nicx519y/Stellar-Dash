// Match Next.js CommonJS default import interop in the Sucrase host harness.
require('jszip').default = require('jszip');
const test = require('node:test');
const assert = require('node:assert/strict');
const { firmwareRuntime: mock } = require('../lib/admin/firmware-mock.ts');
const { loadAllFirmwareReleases } = require('../lib/admin/firmware-list.ts');

test('admin list loads every release while the API keeps its 100-item request limit', async () => {
  const all = Array.from({ length: 205 }, (_, index) => ({ id: `release-${index}` }));
  const calls = [];
  const result = await loadAllFirmwareReleases(async query => {
    calls.push(query);
    return { items: all.slice(query.offset, query.offset + query.limit), total: all.length, offset: query.offset, limit: query.limit };
  }, { query: 'XORA', status: 'draft' });
  assert.deepEqual(result.map(item => item.id), all.map(item => item.id));
  assert.deepEqual(calls.map(call => [call.offset, call.limit]), [[0, 100], [100, 100], [200, 100]]);
  assert.ok(calls.every(call => call.query === 'XORA' && call.status === 'draft'));
});

test('stale admin searches stop before requesting another batch', async () => {
  let current = true; let calls = 0;
  const result = await loadAllFirmwareReleases(async query => {
    calls++;
    current = false;
    return { items: [{ id: 'old-result' }], total: 101, offset: query.offset, limit: query.limit };
  }, {}, () => current);
  assert.equal(result, null);
  assert.equal(calls, 1);
});

test('mock follows the draft / published / withdrawn catalog lifecycle with revision checks', async () => {
  const values = new Map();
  global.sessionStorage = { getItem: key => values.get(key) || null, setItem: (key, value) => values.set(key, value) };
  const draft = (await mock.list({ status: 'draft' })).items[0];
  assert.ok(!(await mock.catalog()).items.some(r => r.id === draft.id));
  const edited = await mock.edit(draft.id, draft.revision, 'Public notes');
  assert.equal(edited.acceptance, draft.acceptance);
  await assert.rejects(mock.publish(draft.id, draft.revision), /changed/);
  const published = await mock.publish(edited.id, edited.revision);
  const visible = (await mock.catalog()).items.find(r => r.id === draft.id);
  assert.equal(visible.notes, 'Public notes'); assert.equal(visible.acceptance, undefined);
  await assert.rejects(mock.remove(published.id, edited.revision), /changed/);
  const withdrawn = await mock.withdraw(published.id, published.revision);
  assert.equal(withdrawn.reason, '');
  assert.ok(!(await mock.catalog()).items.some(r => r.id === draft.id));
  assert.equal((await mock.publish(withdrawn.id, withdrawn.revision)).status, 'published');
});

test('mock withdrawal accepts empty reasons and preserves legacy reasons and state guards', async () => {
  const values = new Map();
  global.sessionStorage = { getItem: key => values.get(key) || null, setItem: (key, value) => values.set(key, value) };
  let release = (await mock.list({ status: 'published' })).items[0];
  for (const reason of ['', '   ', ' Legacy reason ']) {
    const withdrawn = await mock.withdraw(release.id, release.revision, reason);
    assert.equal(withdrawn.status, 'withdrawn'); assert.equal(withdrawn.reason, reason.trim());
    await assert.rejects(mock.withdraw(withdrawn.id, withdrawn.revision), /Only published/);
    release = await mock.publish(withdrawn.id, withdrawn.revision);
  }
  await assert.rejects(mock.withdraw(release.id, release.revision - 1), /changed/);
  await assert.rejects(mock.withdraw(release.id, release.revision, 'x'.repeat(1001)), /reason/);
});

test('mock deletes every release state and retains a deletion audit', async () => {
  const values = new Map();
  global.sessionStorage = { getItem: key => values.get(key) || null, setItem: (key, value) => values.set(key, value) };
  const items = (await mock.list()).items;
  const withdrawn = await mock.withdraw(items.find(r => r.status === 'published').id, 1, 'Issue');
  for (const release of [(await mock.list({ status: 'draft' })).items[0], withdrawn, (await mock.list({ status: 'published' })).items[0]]) {
    await assert.rejects(mock.remove(release.id, release.revision - 1), /changed/);
    await mock.remove(release.id, release.revision);
    await assert.rejects(mock.detail(release.id), /not found/);
    assert.ok(!(await mock.list()).items.some(item => item.id === release.id));
    assert.ok(!(await mock.catalog()).items.some(item => item.id === release.id));
  }
  const audit = JSON.parse(values.get('xora-preview-firmware-releases-v1-deleted'));
  assert.deepEqual(audit.map(item => item.status), ['draft', 'withdrawn', 'published']);
  assert.ok(audit.every(item => item.audit[0].action === 'delete'));
});

test('hosted catalog is same-origin, uncached and never sends device commands', async () => {
  const { firmwareRuntime: hosted } = require('../lib/admin/firmware-hosted.ts');
  const original = global.fetch; const requests = [];
  global.fetch = async (url, options) => {
    requests.push({ url, options });
    return { ok: true, status: 200, json: async () => ({ success: true, data: { items: [], total: 0, limit: 20, offset: 0 } }) };
  };
  try {
    await hosted.catalog({ hardware: '2.0.0' });
    assert.equal(requests[0].url, '/api/firmware-releases?hardware=2.0.0');
    assert.equal(requests[0].options.cache, 'no-store');
    assert.equal(requests[0].options.credentials, 'same-origin');
    assert.equal(requests[0].options.headers.Authorization, undefined);
  } finally { global.fetch = original; }
});

test('hosted draft saves carry the existing legacy acceptance value', async () => {
  const { firmwareRuntime: hosted } = require('../lib/admin/firmware-hosted.ts');
  const original = global.fetch; let request;
  global.fetch = async (url, options) => {
    request = { url, options };
    return { ok: true, status: 200, json: async () => ({ success: true, data: {} }) };
  };
  try {
    await hosted.edit('draft-1', 7, 'Updated notes', 'Historical evidence');
    assert.equal(request.url, '/api/admin/firmware/releases/draft-1');
    assert.equal(request.options.method, 'PATCH');
    assert.deepEqual(JSON.parse(request.options.body), { revision: 7, notes: 'Updated notes', acceptance: 'Historical evidence' });
  } finally { global.fetch = original; }
});

test('catalog and administration routes remain outside the HID provider', () => {
  const fs = require('node:fs'); const path = require('node:path');
  const layout = fs.readFileSync(path.join(__dirname, '../app/layout.tsx'), 'utf8');
  assert.match(layout, /pathname\.startsWith\('\/admin\/'\)/);
  assert.match(layout, /isEmailVerification \|\| isAdministration \|\| isFirmwareCatalog/);
  for (const file of ['../components/firmware-release-catalog.tsx', '../app/admin/firmware/page.tsx', '../components/admin/firmware-detail-page.tsx']) {
    const source = fs.readFileSync(path.join(__dirname, file), 'utf8');
    assert.doesNotMatch(source, /useGamepadConfig|navigator\.hid|uploadFirmwareToDevice|uploadCh585Firmware/);
  }
});
