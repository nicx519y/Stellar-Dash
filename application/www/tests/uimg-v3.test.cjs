const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { buildUimgV3, parseUimgV3, UIMG_WIDTH, UIMG_HEIGHT } = require('../lib/uimg-v3.ts');
const {
  advanceHoldGesture,
  advanceHoldProgress,
  HOLD_TO_INSTALL_MS,
  isShortSelectionPress,
} = require('../lib/hold-progress.ts');
const { calculateImageCoverRect } = require('../lib/image-cover.ts');
const { findRgb565ContentBounds } = require('../lib/screen-control-image.ts');
const {
  beginDeviceImageInstall,
  deviceImageInstallRingProgress,
  finishDeviceImageInstall,
  getDeviceImageInstallState,
  updateDeviceImageInstall,
} = require('../lib/device-image-install-lock.ts');

const frameBytes = UIMG_WIDTH * UIMG_HEIGHT * 2;

test('image cover crop fills 320x172 without letterboxing', () => {
  assert.deepEqual(calculateImageCoverRect(320, 172, 320, 172), {
    sourceX: 0, sourceY: 0, sourceWidth: 320, sourceHeight: 172,
  });
  const portrait = calculateImageCoverRect(300, 600, 320, 172);
  assert.equal(portrait.sourceX, 0);
  assert.equal(portrait.sourceWidth, 300);
  assert.ok(portrait.sourceY > 0);
  assert.ok(portrait.sourceHeight < 600);
  assert.equal(portrait.sourceWidth / portrait.sourceHeight, 320 / 172);
  const wide = calculateImageCoverRect(2400, 1080, 320, 172);
  assert.equal(wide.sourceY, 0);
  assert.equal(wide.sourceHeight, 1080);
  assert.ok(wide.sourceX > 0);
  assert.ok(wide.sourceWidth < 2400);
  assert.equal(wide.sourceWidth / wide.sourceHeight, 320 / 172);
});

test('legacy RGB565 letterbox is detected without cropping ordinary images', () => {
  const width = 320;
  const height = 172;
  const letterboxed = new Uint8Array(width * height * 2);
  for (let y = 0; y < height; y++) {
    for (let x = 7; x < 312; x++) {
      const offset = (y * width + x) * 2;
      letterboxed[offset] = 0xff;
      letterboxed[offset + 1] = 0xff;
    }
  }
  assert.deepEqual(findRgb565ContentBounds(letterboxed, width, height), {
    x: 7,
    y: 0,
    width: 305,
    height: 172,
  });

  const full = new Uint8Array(width * height * 2).fill(0xff);
  assert.deepEqual(findRgb565ContentBounds(full, width, height), {
    x: 0,
    y: 0,
    width,
    height,
  });
});

test('UIMG v3 round-trips static and six-frame payloads', () => {
  for (const frames of [1, 6]) {
    const payload = new Uint8Array(frameBytes * frames);
    payload.forEach((_, index) => { payload[index] = index & 0xff; });
    const parsed = parseUimgV3(buildUimgV3(payload, frames, frames === 1 ? 0 : 3));
    assert.equal(parsed.frameCount, frames);
    assert.equal(parsed.payloadBytes, payload.byteLength);
    assert.deepEqual(parsed.payload, payload);
  }
});

test('UIMG v3 rejects a seventh frame and payload/header tampering', () => {
  assert.throws(() => buildUimgV3(new Uint8Array(frameBytes * 7), 7, 3), /Invalid UIMG/);
  const valid = buildUimgV3(new Uint8Array(frameBytes), 1, 0);
  const payloadDamage = valid.slice();
  payloadDamage[payloadDamage.length - 1] ^= 1;
  assert.throws(() => parseUimgV3(payloadDamage), /payload CRC/);
  const headerDamage = valid.slice();
  headerDamage[83] = 1;
  assert.throws(() => parseUimgV3(headerDamage), /metadata|header CRC/);
});

test('hold progress waits 0.5 seconds, grows for 1.5 seconds, decays, and resumes', () => {
  assert.equal(HOLD_TO_INSTALL_MS, 2000);
  let state = advanceHoldGesture({ progress: 0, delayMs: 0 }, true, 499);
  assert.deepEqual(state, { progress: 0, delayMs: 499 });
  state = advanceHoldGesture(state, true, 1);
  assert.deepEqual(state, { progress: 0, delayMs: 500 });
  state = advanceHoldGesture(state, true, 750);
  assert.equal(state.progress, 0.5);
  state = advanceHoldGesture(state, false, 375);
  assert.equal(state.progress, 0.25);
  state = advanceHoldGesture(state, true, 1125);
  assert.equal(state.progress, 1);
  assert.equal(advanceHoldProgress(0.3, false, 450), 0);
});

test('only a fresh press released before 0.5 seconds toggles selection', () => {
  assert.equal(isShortSelectionPress(0, false), true);
  assert.equal(isShortSelectionPress(499, false), true);
  assert.equal(isShortSelectionPress(500, false), false);
  assert.equal(isShortSelectionPress(100, true), false);
});

test('one monotonic device transfer lock survives component ownership changes', () => {
  assert.equal(beginDeviceImageInstall('image-a'), true);
  assert.equal(beginDeviceImageInstall('image-b'), false);
  updateDeviceImageInstall('image-a', 60);
  updateDeviceImageInstall('image-a', 40);
  assert.deepEqual(getDeviceImageInstallState(), { imageId: 'image-a', progress: 60 });
  finishDeviceImageInstall('image-b');
  assert.notEqual(getDeviceImageInstallState(), null);
  finishDeviceImageInstall('image-a');
  assert.equal(getDeviceImageInstallState(), null);
});

test('device transfer ring drains from full to empty as upload completes', () => {
  assert.equal(deviceImageInstallRingProgress(0), 1);
  assert.equal(deviceImageInstallRingProgress(40), 0.6);
  assert.equal(deviceImageInstallRingProgress(100), 0);
  assert.equal(deviceImageInstallRingProgress(-20), 1);
  assert.equal(deviceImageInstallRingProgress(120), 0);
});

test('hold and device transfer progress use a hollow transparent ring', () => {
  const source = fs.readFileSync(path.join(
    __dirname,
    '..',
    'components',
    'background-image-gallery.tsx',
  ), 'utf8');
  const ringStart = source.indexOf('background: `conic-gradient(');
  const ringEnd = source.indexOf('/>', ringStart);
  assert.ok(ringStart >= 0 && ringEnd > ringStart);
  const ring = source.slice(ringStart, ringEnd);
  assert.match(ring, /WebkitMask: 'radial-gradient\(farthest-side, transparent calc\(100% - 5px\), #000 0\)'/);
  assert.match(ring, /mask: 'radial-gradient\(farthest-side, transparent calc\(100% - 5px\), #000 0\)'/);
  assert.doesNotMatch(ring, /bg="gray\.900"/);
});

test('device preview matches its catalog fingerprint and renders the server source without reading device pixels', () => {
  const source = fs.readFileSync(path.join(
    __dirname,
    '..',
    'components',
    'background-image-gallery.tsx',
  ), 'utf8');
  assert.match(source, /fetchGalleryImageMatch\(authorizedFetch,/);
  assert.match(source, /loadGallerySourcePreview\(image, authorizedFetch\)/);
  assert.doesNotMatch(source, /readDeviceImage\('user'/);
  assert.doesNotMatch(source, /rgb565ToPngDataUrl/);
  assert.match(source, /objectFit="cover"/);
  const successStart = source.indexOf("let installedPreview = '';");
  const failureStart = source.indexOf('} catch (error) {', successStart);
  assert.ok(successStart >= 0 && failureStart > successStart);
  const successPath = source.slice(successStart, failureStart);
  assert.match(successPath, /Math\.min\(98,/);
  assert.match(successPath, /setCurrentPreview\(installedPreview\)/);
  assert.doesNotMatch(successPath, /await syncDevice\(\)/);
});

test('device installation supersedes preview synchronization and recovery keeps the root error', () => {
  const source = fs.readFileSync(path.join(
    __dirname,
    '..',
    'components',
    'background-image-gallery.tsx',
  ), 'utf8');
  assert.match(source, /const syncInFlight = useRef<Promise<void> \| null>\(null\)/);
  assert.match(source, /const syncEpoch = useRef\(0\)/);
  assert.match(source, /if \(getDeviceImageInstallState\(\)\) return Promise\.resolve\(\)/);
  assert.doesNotMatch(source, /if \(syncInFlight\.current\) await syncInFlight\.current/);
  assert.match(source, /syncEpoch\.current \+= 1/);
  assert.match(source, /if \(!isCurrent\(\)\) \{/);
  assert.match(source, /finishDeviceImageInstall\(image\.id\);\s+await syncDevice\(true\)/);
  assert.match(source, /if \(!silent\) \{\s+showToast\(\{ title: zh \? '读取设备图片失败'/);
});

test('starting device installation removes its source from batch selection', () => {
  const source = fs.readFileSync(path.join(
    __dirname,
    '..',
    'components',
    'background-image-gallery.tsx',
  ), 'utf8');
  const begin = source.indexOf('if (!beginDeviceImageInstall(image.id)) return;');
  const upload = source.indexOf('await uploadDeviceImage({', begin);
  assert.ok(begin >= 0 && upload > begin);
  const installationStart = source.slice(begin, upload);
  assert.match(installationStart, /setSelected\(current => \{/);
  assert.match(installationStart, /next\.delete\(image\.id\)/);
});

test('device preview keeps 320x172 physical image pixels inside a two-pixel interactive frame', () => {
  const source = fs.readFileSync(path.join(
    __dirname,
    '..',
    'components',
    'background-image-gallery.tsx',
  ), 'utf8');
  assert.match(source, /DEVICE_SCREEN_WIDTH \/ displayPixelRatio/);
  assert.match(source, /DEVICE_SCREEN_HEIGHT \/ displayPixelRatio/);
  assert.match(source, /Math\.max\(1, Number\(window\.devicePixelRatio\) \|\| 1\)/);
  const previewStart = source.indexOf('const currentName =');
  const dialogStart = source.indexOf('<Dialog.Root', previewStart);
  const previewMarkup = source.slice(previewStart, dialogStart);
  assert.match(previewMarkup, /boxSizing="content-box"/);
  assert.match(previewMarkup, /borderWidth="2px"/);
  assert.match(previewMarkup, /borderColor="gray\.600"/);
  assert.match(previewMarkup, /padding="2px"/);
  assert.match(previewMarkup, /_hover=\{\{ borderColor: 'green\.400' \}\}/);
});

test('gallery persists and restores the last selected tab in local storage', () => {
  const source = fs.readFileSync(path.join(
    __dirname,
    '..',
    'components',
    'background-image-gallery.tsx',
  ), 'utf8');
  assert.match(source, /const GALLERY_TAB_STORAGE_KEY = 'hbox-background-gallery-tab-v1'/);
  assert.match(source, /localStorage\.getItem\(GALLERY_TAB_STORAGE_KEY\)/);
  assert.match(source, /stored === 'system' \|\| stored === 'mine'/);
  assert.match(source, /localStorage\.setItem\(GALLERY_TAB_STORAGE_KEY, value\)/);
  assert.match(source, /<Tabs\.Root value=\{galleryTab\} onValueChange=\{details => rememberGalleryTab\(details\.value\)\}>/);
  assert.doesNotMatch(source, /<Tabs\.Root defaultValue="system">/);
});

test('gallery tab toolbar keeps the hold hint on a stable baseline', () => {
  const source = fs.readFileSync(path.join(
    __dirname,
    '..',
    'components',
    'background-image-gallery.tsx',
  ), 'utf8');
  const toolbarStart = source.indexOf('const galleryToolbar =');
  const currentNameStart = source.indexOf('const currentName =', toolbarStart);
  assert.ok(toolbarStart >= 0 && currentNameStart > toolbarStart);
  const toolbar = source.slice(toolbarStart, currentNameStart);
  assert.match(toolbar, /height="28px"/);
  assert.match(toolbar, /minHeight="28px"/);
  assert.match(toolbar, /<Box width="160px" height="28px" flexShrink="0">/);
  assert.match(source, /\{galleryToolbar\(false\)\}/);
  assert.match(source, /\{galleryToolbar\(session\.authenticated\)\}/);
});
