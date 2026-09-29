const test = require('node:test');
const assert = require('node:assert/strict');
const { clearDeviceImagePreviewMemory, loadDeviceImagePreview } = require('../lib/device-image-preview-cache.ts');
const { rememberInstalledSystemImage, findInstalledGalleryImage } = require('../lib/installed-system-image.ts');

const identity = { deviceId: null, sessionId: 'session-a' };
const deviceImage = { width: 320, height: 172, frameCount: 39, fps: 12, payloadBytes: 650744, payloadCrc32: 1234 };
const fp = '320:172:650744:39:12:1234';
const image = { id: 'system-gif', scope: 'system', sourceUrl: '/api/gallery/images/system-gif/source', fps: 3, frameCount: 10, payloadBytes: 1100800, payloadCrc32: 5678 };

function storage(t) {
  const previous = globalThis.localStorage;
  const values = new Map();
  globalThis.localStorage = {
    getItem: key => values.get(key) ?? null,
    setItem: (key, value) => values.set(key, value),
    removeItem: key => values.delete(key),
  };
  t.after(() => {
    clearDeviceImagePreviewMemory();
    if (previous === undefined) delete globalThis.localStorage;
    else globalThis.localStorage = previous;
  });
  return values;
}

function api(matched, pages = []) {
  const calls = [];
  const fetch = async (url) => {
    calls.push(url);
    const data = url.startsWith('/api/gallery/match?') ? { item: matched } : pages.shift();
    assert.ok(data, `unexpected request: ${url}`);
    return new Response(JSON.stringify({ success: true, data }));
  };
  return { fetch, calls };
}

test('converted system GIF survives document reload and a new device session without storing pixels', async t => {
  const values = storage(t);
  rememberInstalledSystemImage(fp, image);
  assert.deepEqual(JSON.parse([...values.values()][0]), [{ fingerprint: fp, imageId: image.id }]);
  clearDeviceImagePreviewMemory();
  const reconnected = { ...identity, sessionId: 'new-session' };
  assert.equal(loadDeviceImagePreview(reconnected, fp), null);
  const { fetch, calls } = api(null, [{ items: [], nextCursor: 'page-2' }, { items: [image], nextCursor: null }]);
  assert.deepEqual(await findInstalledGalleryImage(fetch, fp, deviceImage), image);
  assert.equal(calls.length, 3);
  assert.match(calls[2], /cursor=page-2/);
});

test('a replaced device image cannot reuse an association for different bytes', async t => {
  storage(t);
  rememberInstalledSystemImage(fp, image);
  const changed = api(null);
  assert.equal(await findInstalledGalleryImage(changed.fetch, 'different-crc', deviceImage), null);
  assert.equal(changed.calls.length, 1);
});

test('deleted or unpublished system images are not restored from a local reference', async t => {
  const values = storage(t);
  rememberInstalledSystemImage(fp, image);
  const { fetch } = api(null, [{ items: [], nextCursor: null }]);
  assert.equal(await findInstalledGalleryImage(fetch, fp, deviceImage), null);
  assert.deepEqual(JSON.parse([...values.values()][0]), []);
});

test('canonical matching still restores static and personal images directly', async t => {
  storage(t);
  for (const scope of ['system', 'user']) {
    const matched = { ...image, scope, fps: 0, frameCount: 1 };
    const { fetch, calls } = api(matched);
    assert.deepEqual(await findInstalledGalleryImage(fetch, fp, deviceImage), matched);
    assert.equal(calls.length, 1);
  }
});

test('storage failure or corrupt data does not prevent canonical matching', async t => {
  const values = storage(t);
  rememberInstalledSystemImage(fp, image);
  values.set([...values.keys()][0], '{');
  const { fetch } = api(image);
  assert.deepEqual(await findInstalledGalleryImage(fetch, fp, deviceImage), image);
  globalThis.localStorage.getItem = () => { throw new Error('blocked'); };
  globalThis.localStorage.setItem = () => { throw new Error('blocked'); };
  rememberInstalledSystemImage(fp, image);
  assert.deepEqual(await findInstalledGalleryImage(fetch, fp, deviceImage), image);
});

test('temporary authorization failure retains the association for retry', async t => {
  const values = storage(t);
  rememberInstalledSystemImage(fp, image);
  const denied = async () => new Response(JSON.stringify({ success: false, error: 'FORBIDDEN' }), { status: 403 });
  await assert.rejects(findInstalledGalleryImage(denied, fp, deviceImage), /HTTP 403/);
  assert.equal(values.size, 1);
});

test('references are bounded metadata and never persist personal gallery IDs', t => {
  const values = storage(t);
  rememberInstalledSystemImage(fp, { ...image, scope: 'user' });
  assert.equal(values.size, 0);
  for (let i = 0; i < 40; i++) rememberInstalledSystemImage('fp-' + i, image);
  const stored = JSON.parse([...values.values()][0]);
  assert.equal(stored.length, 32);
  assert.equal(stored[0].fingerprint, 'fp-8');
  assert.equal(stored.at(-1).fingerprint, 'fp-39');
});
