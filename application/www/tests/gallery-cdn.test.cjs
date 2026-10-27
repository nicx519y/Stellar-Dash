const test = require('node:test');
const assert = require('node:assert/strict');
const { fetchGalleryAsset, galleryPosterUrl } = require('../lib/image-gallery.ts');

const image = { id: '50000000-0000-4000-8000-000000000000', scope: 'system' };
const base = `https://assets.example.com/cdn/gallery/${image.id}/abc-aaaaaaaaaaaaaaaa`;

test('public CDN assets use CORS without credentials, headers or redirects', async t => {
  const originalFetch = global.fetch;
  t.after(() => { global.fetch = originalFetch; });
  const calls = [];
  global.fetch = async (...args) => { calls.push(args); return new Response('asset'); };
  const authorizedFetch = () => { throw new Error('CDN must bypass device authorization'); };
  for (const variant of ['source', 'preview', 'device']) {
    const response = await fetchGalleryAsset(image, `${base}/${variant}`, authorizedFetch, {
      credentials: 'include', headers: { Authorization: 'Bearer must-not-leak' }, redirect: 'follow',
    });
    assert.equal(await response.text(), 'asset');
  }
  const poster = galleryPosterUrl({ sourceMime: 'image/gif', previewUrl: base + '/preview' });
  await fetchGalleryAsset(image, poster, authorizedFetch);
  assert.equal(calls[3][0], base + '/preview?poster=frame-3');
  for (const [, init] of calls) {
    assert.equal(init.credentials, 'omit');
    assert.equal(init.headers, undefined);
    assert.equal(init.redirect, 'error');
    assert.equal(init.mode, 'cors');
    assert.equal(init.cache, 'force-cache');
  }
});

test('same-origin and Mock official URLs keep the existing device resource channel', async () => {
  const calls = [];
  const authorizedFetch = async (...args) => { calls.push(args); return new Response('authorized'); };
  for (const url of ['/api/gallery/images/id/source', '/mock/gallery/preview.png']) {
    assert.equal(await (await fetchGalleryAsset(image, url, authorizedFetch)).text(), 'authorized');
  }
  assert.equal(calls.length, 2);
});

test('personal assets retain same-origin cookie access', async t => {
  const originalFetch = global.fetch;
  t.after(() => { global.fetch = originalFetch; });
  let received;
  global.fetch = async (...args) => { received = args; return new Response('private'); };
  const url = `/api/gallery/images/${image.id}/source`;
  await fetchGalleryAsset({ ...image, scope: 'user' }, url, () => { throw new Error('device'); });
  assert.equal(received[0], url);
  assert.equal(received[1].credentials, 'same-origin');
  assert.equal(received[1].redirect, 'error');
});

test('CDN fetch rejects insecure URLs and paths outside the public asset namespace', () => {
  for (const url of [base.replace('https:', 'http:') + '/source',
    'https://assets.example.com/api/gallery/images/id/source', base + '/unknown',
    base.replace(image.id, 'someone-else') + '/source', base + '/source/extra',
    base.replace('assets.example.com', 'user@assets.example.com') + '/source']) {
    assert.throws(() => fetchGalleryAsset(image, url, () => { throw new Error('device'); }), /Invalid public gallery/);
  }
});
