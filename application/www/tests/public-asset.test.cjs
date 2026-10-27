const assert = require('node:assert/strict');
const test = require('node:test');
const { publicAssetUrl } = require('../lib/public-asset.ts');
const base = 'https://assets.example.com/cdn/web/' + 'a'.repeat(32);

test('public images/fonts use immutable build prefix while API and service worker retain their origin', () => {
  for (const resource of ['/images/logo.svg', '/images/account-avatars/sf6-ryu.webp', '/fonts/custom_en.ttf']) {
    assert.equal(publicAssetUrl(resource, base), base + resource);
    assert.equal(publicAssetUrl(resource, ''), resource);
  }
  for (const resource of ['/api/gallery/mine', '/uploads/private.png', '/webhid-network-trace-sw.js', 'blob:preview', 'https://example.com/image.png']) {
    assert.equal(publicAssetUrl(resource, base), resource);
  }
  assert.throws(() => publicAssetUrl('/images/../private', base));
});
