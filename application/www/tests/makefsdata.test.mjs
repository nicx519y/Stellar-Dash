import assert from 'node:assert/strict';
import { existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import test from 'node:test';

import {
	assertWebResourceBudget,
	buildExternalWebResources,
	createAssetManifestFromHtml,
	evaluateWebResourceBudget,
	getContentType,
	makeFileBuffer,
	shouldCompressExtension,
	shouldIncludeFile,
	WEB_RESOURCES_PHYSICAL_MAX_BYTES,
	WEB_RESOURCES_TARGET_BYTES,
} from '../makefsdata.js';

const buildRoot = resolve('build');

const entryHtml = `
	<link rel="stylesheet" href="/_next/static/css/app.abc123.css">
	<link rel="preload" as="script" href="/_next/static/js/main-app.abc123.js">
	<script src="/_next/static/js/app/layout.abc123.js"></script>
	<script src="/_next/static/js/app/page.root123.js?cache=1"></script>
	<script src="/_next/static/chunks/polyfills-abc123.js"></script>
	<img src="/images/inline-hud.svg">
	<a href="/lighting">Lighting</a>
	<script src="https://cdn.example.com/untrusted.js"></script>
	<img src="data:image/png;base64,AAAA">
`;
const entryManifest = createAssetManifestFromHtml(entryHtml);

test('derives the exact root SPA asset manifest', () => {
	for (const referencedPath of [
		'index.html',
		'_next/static/css/app.abc123.css',
		'_next/static/js/main-app.abc123.js',
		'_next/static/js/app/layout.abc123.js',
		'_next/static/js/app/page.root123.js',
		'_next/static/chunks/polyfills-abc123.js',
		'images/inline-hud.svg',
		'fonts/custom_en.ttf',
		'images/cyber-scene.webp',
	]) {
		assert.equal(
			shouldIncludeFile(join(buildRoot, referencedPath), buildRoot, entryManifest),
			true,
			`expected manifest to include /${referencedPath}`,
		);
	}
});

test('excludes route export duplicates and unrelated static files', () => {
	for (const redundantPath of [
		'lighting/index.html',
		'_next/static/js/app/lighting/page.duplicate.js',
		'_next/static/js/app/keys/page.duplicate.js',
		'_next/static/js/app/layout.unreferenced.js',
		'images/cyber-scene-copy.webp',
		'images/unrelated.webp',
	]) {
		assert.equal(
			shouldIncludeFile(join(buildRoot, redundantPath), buildRoot, entryManifest),
			false,
			`expected manifest to exclude /${redundantPath}`,
		);
	}
});

test('ignores navigation, external, and inline-data references', () => {
	assert.equal(
		entryManifest.has('/lighting'),
		false,
	);
	assert.equal(
		entryManifest.has('/untrusted.js'),
		false,
	);
	assert.equal(
		[...entryManifest].some(path => path.startsWith('data:')),
		false,
	);
});

test('serves WebP with its MIME type and does not deflate it again', () => {
	assert.equal(getContentType('webp'), 'image/webp');
	assert.equal(shouldCompressExtension('webp'), false);

	const source = Buffer.from([0x52, 0x49, 0x46, 0x46]);
	const output = makeFileBuffer(
		'/images/cyber-scene.webp\0\0',
		'webp',
		false,
		source,
	);
	const serialized = output.toString('latin1');
	assert.match(serialized, /Content-Type: image\/webp\r\n/);
	assert.doesNotMatch(serialized, /Content-Encoding: deflate\r\n/);
	assert.deepEqual(output.subarray(-source.length), source);
});

test('reports target and physical slot boundaries exactly', () => {
	const atTarget = evaluateWebResourceBudget(WEB_RESOURCES_TARGET_BYTES);
	assert.equal(atTarget.withinTarget, true);
	assert.equal(atTarget.withinPhysicalLimit, true);
	assert.equal(atTarget.targetHeadroomBytes, 0);

	const overTarget = assertWebResourceBudget(WEB_RESOURCES_TARGET_BYTES + 1);
	assert.equal(overTarget.withinTarget, false);
	assert.equal(overTarget.withinPhysicalLimit, true);
	assert.throws(
		() => assertWebResourceBudget(WEB_RESOURCES_TARGET_BYTES + 1, { requireTarget: true }),
		/acceptance target/,
	);

	assert.doesNotThrow(
		() => assertWebResourceBudget(WEB_RESOURCES_PHYSICAL_MAX_BYTES),
	);
	assert.throws(
		() => assertWebResourceBudget(WEB_RESOURCES_PHYSICAL_MAX_BYTES + 1),
		/physical .* slot/,
	);
});

test('compatibility packing writes only the immutable external payload', () => {
	const root = mkdtempSync(join(tmpdir(), 'hbox-webresources-'));
	try {
		const hosted = join(root, 'build');
		const output = join(root, 'application', 'Libs', 'httpd', 'ex_fsdata.bin');
		mkdirSync(join(hosted, '_next', 'static', 'js'), { recursive: true });
		writeFileSync(
			join(hosted, 'index.html'),
			'<script src="/_next/static/js/app.js"></script>',
			'utf8',
		);
		writeFileSync(join(hosted, '_next', 'static', 'js', 'app.js'), 'self.HBox=true;', 'utf8');

		const result = buildExternalWebResources({ buildRoot: hosted, outputPath: output });
		assert.equal(result.outputPath, resolve(output));
		assert.deepEqual(result.files, ['_next/static/js/app.js', 'index.html']);
		assert.equal(readFileSync(output).byteLength, result.byteLength);
		assert.equal(existsSync(join(root, 'application', 'Libs', 'httpd', 'fsdata.c')), false);
	} finally {
		rmSync(root, { recursive: true, force: true });
	}
});
