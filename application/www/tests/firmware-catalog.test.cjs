// Match Next.js CommonJS default import interop in the Sucrase host harness.
require('jszip').default = require('jszip');
const test = require('node:test');
const assert = require('node:assert/strict');
const { firmwareRuntime: mock } = require('../lib/admin/firmware-mock.ts');

test('mock follows the draft / published / withdrawn catalog lifecycle with revision checks', async () => {
  const values = new Map();
  global.sessionStorage = { getItem: key => values.get(key) || null, setItem: (key, value) => values.set(key, value) };
  const draft = (await mock.list({ status: 'draft' })).items[0];
  assert.ok(!(await mock.catalog()).items.some(r => r.id === draft.id));
  const edited = await mock.edit(draft.id, draft.revision, 'Public notes', 'Private evidence');
  await assert.rejects(mock.publish(draft.id, draft.revision), /changed/);
  const published = await mock.publish(edited.id, edited.revision);
  const visible = (await mock.catalog()).items.find(r => r.id === draft.id);
  assert.equal(visible.notes, 'Public notes'); assert.equal(visible.acceptance, undefined);
  await assert.rejects(mock.remove(published.id, published.revision), /draft/);
  const withdrawn = await mock.withdraw(published.id, published.revision, 'Issue');
  assert.ok(!(await mock.catalog()).items.some(r => r.id === draft.id));
  assert.equal((await mock.publish(withdrawn.id, withdrawn.revision)).status, 'published');
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

test('catalog and administration routes remain outside the HID provider', () => {
  const fs = require('node:fs'); const path = require('node:path');
  const layout = fs.readFileSync(path.join(__dirname, '../app/layout.tsx'), 'utf8');
  assert.match(layout, /pathname\.startsWith\('\/admin\/'\)/);
  assert.match(layout, /isEmailVerification \|\| isAdministration \|\| isFirmwareCatalog/);
  for (const file of ['../components/firmware-release-catalog.tsx', '../app/admin/firmware/page.tsx']) {
    const source = fs.readFileSync(path.join(__dirname, file), 'utf8');
    assert.doesNotMatch(source, /useGamepadConfig|navigator\.hid|uploadFirmwareToDevice|uploadCh585Firmware/);
  }
});
