const test = require('node:test');
const assert = require('node:assert/strict');
const { fetchReleaseBundle } = require('../lib/device-transport/release-download.ts');
const release = { id: '50000000-0000-4000-8000-000000000000', bundleSha256: 'a'.repeat(64) };
const base = 'https://assets.example.com/cdn/web/' + 'b'.repeat(32);
const url = `https://assets.example.com/cdn/firmware/${release.id}/${release.bundleSha256}/release.zip`;

test('firmware CDN fetch is anonymous, redirect-free, and bound to configured origin and digest', async t => {
  const calls = [];
  t.mock.method(global, 'fetch', async (...args) => { calls.push(args); return new Response('zip'); });
  const authorized = () => { throw new Error('must not send device authorization to CDN'); };
  assert.equal(await (await fetchReleaseBundle({ ...release, downloadUrl: url }, authorized, base)).text(), 'zip');
  assert.equal(calls[0][0], url);
  assert.deepEqual(calls[0][1], { method: 'GET', mode: 'cors', credentials: 'omit', redirect: 'error', cache: 'default' });
  for (const downloadUrl of [url.replace('https:', 'http:'), url.replace('assets.example.com', 'evil.example.com'),
    url.replace(release.bundleSha256, 'b'.repeat(64)), url + '?token=secret', url + '#fragment',
    url.replace('/release.zip', '/../release.zip'), url.replace('https://', 'https://user:password@')]) {
    assert.throws(() => fetchReleaseBundle({ ...release, downloadUrl }, authorized, base), /Invalid firmware CDN URL/);
  }
  assert.throws(() => fetchReleaseBundle({ ...release, downloadUrl: url }, authorized, ''), /Invalid firmware CDN URL/);
  assert.equal(calls.length, 1);
});

test('ordinary and Mock deployments retain the origin download channel', async () => {
  const calls = [];
  await fetchReleaseBundle(release, async (...args) => { calls.push(args); return new Response('zip'); }, '');
  assert.deepEqual(calls, [[`/api/firmware-releases/${release.id}/download`, { cache: 'no-store' }]]);
});
