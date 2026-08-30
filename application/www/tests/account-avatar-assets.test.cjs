'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');

const EXPECTED = [
  'ryu','luke','jamie','chun-li','guile','kimberly','juri','ken','blanka',
  'dhalsim','e-honda','dee-jay','manon','marisa','jp','zangief','lily',
  'cammy','rashid','aki','ed','akuma','m-bison','terry','mai','elena','sagat',
  'c-viper','alex','ingrid','yasmine',
].map(slug => `sf6-${slug}`);

function webpDimensions(buffer) {
  assert.equal(buffer.toString('ascii', 0, 4), 'RIFF');
  assert.equal(buffer.toString('ascii', 8, 12), 'WEBP');
  const kind = buffer.toString('ascii', 12, 16);
  if (kind === 'VP8X') {
    return [1 + buffer.readUIntLE(24, 3), 1 + buffer.readUIntLE(27, 3)];
  }
  if (kind === 'VP8 ') {
    return [buffer.readUInt16LE(26) & 0x3fff, buffer.readUInt16LE(28) & 0x3fff];
  }
  throw new Error(`unsupported WebP chunk ${kind}`);
}

test('account avatar catalog has 31 exact 256px WebP assets within budget', () => {
  const root = path.resolve('public/images/account-avatars');
  const files = fs.readdirSync(root).sort();
  assert.deepEqual(files, EXPECTED.map(id => `${id}.webp`).sort());
  let total = 0;
  for (const file of files) {
    const buffer = fs.readFileSync(path.join(root, file));
    total += buffer.length;
    assert.deepEqual(webpDimensions(buffer), [256, 256], file);
  }
  assert.ok(total <= 768 * 1024, `avatar assets use ${total} bytes`);
});

test('frontend and server avatar identifiers stay in sync', () => {
  const serverCatalog = require('../../../server/src/account-avatars');
  assert.deepEqual([...serverCatalog.ACCOUNT_AVATARS].sort(), [...EXPECTED].sort());
});

test('signed-in users without a chosen avatar fall back to Ryu', () => {
  const catalogSource = fs.readFileSync(
    path.resolve('lib/user-auth/avatar-catalog.ts'),
    'utf8'
  );
  const controlSource = fs.readFileSync(
    path.resolve('components/user-auth-control.tsx'),
    'utf8'
  );
  assert.match(catalogSource, /DEFAULT_ACCOUNT_AVATAR_ID = 'sf6-ryu'/);
  assert.match(controlSource, /session\.user\.avatarUrl \|\| DEFAULT_ACCOUNT_AVATAR_SRC/);
  assert.match(controlSource, /session\.user\?\.avatarId \|\| DEFAULT_ACCOUNT_AVATAR_ID/);
});
