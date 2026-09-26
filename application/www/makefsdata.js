import fs from 'node:fs';
import { dirname, join, normalize, relative, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

import pako from 'pako';

const scriptPath = fileURLToPath(import.meta.url);
const wwwRoot = dirname(scriptPath);
const applicationRoot = dirname(wwwRoot);
const buildRoot = join(wwwRoot, 'build');
const externalPayloadPath = normalize(join(applicationRoot, 'Libs/httpd/ex_fsdata.bin'));

export const WEB_RESOURCES_TARGET_BYTES = 0x100000;
export const WEB_RESOURCES_PHYSICAL_MAX_BYTES = 0x180000;

const ALWAYS_INCLUDED_ASSETS = Object.freeze([
  'fonts/custom_en.ttf',
  'images/cyber-scene.webp',
]);
const INCLUDED_EXTENSIONS = new Set([
  'html', 'htm', 'css', 'js', 'json', 'svg', 'ico', 'png', 'jpg', 'jpeg',
  'bmp', 'gif', 'ttf', 'webp',
]);
const ALREADY_COMPRESSED_EXTENSIONS = new Set([
  'png', 'jpg', 'jpeg', 'gif', 'webp', 'ico', 'ttf',
]);
const CONTENT_TYPES = new Map([
  ['html', 'text/html'],
  ['htm', 'text/html'],
  ['css', 'text/css'],
  ['js', 'application/javascript'],
  ['json', 'application/json'],
  ['svg', 'image/svg+xml'],
  ['ico', 'image/x-icon'],
  ['png', 'image/png'],
  ['jpg', 'image/jpeg'],
  ['jpeg', 'image/jpeg'],
  ['bmp', 'image/bmp'],
  ['gif', 'image/gif'],
  ['ttf', 'font/ttf'],
  ['webp', 'image/webp'],
]);

export function getContentType(extension) {
  return CONTENT_TYPES.get(normalizeExtension(extension)) ?? 'text/plain';
}

export function shouldCompressExtension(extension) {
  return !ALREADY_COMPRESSED_EXTENSIONS.has(normalizeExtension(extension));
}

export function createAssetManifestFromHtml(html) {
  const manifest = new Set(['index.html', ...ALWAYS_INCLUDED_ASSETS]);
  const attribute = /\b(?:src|href)\s*=\s*(["'])(.*?)\1/gi;
  for (const match of html.matchAll(attribute)) {
    const value = match[2].trim();
    if (!value.startsWith('/') || value.startsWith('//')) continue;
    const path = value.slice(1).split(/[?#]/, 1)[0].replace(/\\/g, '/');
    const extension = normalizeExtension(path);
    if (path && INCLUDED_EXTENSIONS.has(extension)) manifest.add(path);
  }
  return manifest;
}

export function shouldIncludeFile(filePath, root, manifest) {
  const path = relative(root, filePath).replace(/\\/g, '/');
  return manifest.has(path);
}

export function makeFileBuffer(
  paddedQualifiedName,
  extension,
  _isCompressed = false,
  fileContent,
  suppliedCompressed = null,
) {
  const source = Buffer.isBuffer(fileContent) ? fileContent : Buffer.from(fileContent);
  let payload = source;
  let compressed = false;
  if (shouldCompressExtension(extension)) {
    try {
      payload = Buffer.from(suppliedCompressed ?? pako.deflate(source, {
        level: 9,
        windowBits: 15,
        memLevel: 9,
      }));
      compressed = true;
    } catch (error) {
      console.warn(`Compression failed for ${paddedQualifiedName}; storing the original bytes`, error);
    }
  }
  const headers = [
    'HTTP/1.0 200 OK\r\n',
    'Server: IONIX-Hitbox\r\n',
    `Content-Length: ${payload.byteLength}\r\n`,
    compressed ? 'Content-Encoding: deflate\r\n' : '',
    `Content-Type: ${getContentType(extension)}\r\n\r\n`,
  ].join('');
  return Buffer.concat([
    Buffer.from(paddedQualifiedName, 'utf8'),
    Buffer.from(headers, 'utf8'),
    payload,
  ]);
}

export function evaluateWebResourceBudget(byteLength) {
  if (!Number.isSafeInteger(byteLength) || byteLength < 0) {
    throw new TypeError('WebResources byte length must be a non-negative integer');
  }
  return {
    byteLength,
    withinTarget: byteLength <= WEB_RESOURCES_TARGET_BYTES,
    withinPhysicalLimit: byteLength <= WEB_RESOURCES_PHYSICAL_MAX_BYTES,
    targetHeadroomBytes: Math.max(0, WEB_RESOURCES_TARGET_BYTES - byteLength),
    physicalHeadroomBytes: Math.max(0, WEB_RESOURCES_PHYSICAL_MAX_BYTES - byteLength),
  };
}

export function assertWebResourceBudget(byteLength, options = {}) {
  const budget = evaluateWebResourceBudget(byteLength);
  if (!budget.withinPhysicalLimit) {
    throw new Error(
      `WebResources payload ${byteLength} exceeds the physical WebResources slot ` +
      `${WEB_RESOURCES_PHYSICAL_MAX_BYTES}`,
    );
  }
  if (options.requireTarget === true && !budget.withinTarget) {
    throw new Error(
      `WebResources payload ${byteLength} exceeds the acceptance target ` +
      `${WEB_RESOURCES_TARGET_BYTES}`,
    );
  }
  return budget;
}

export function buildExternalWebResources(options = {}) {
  const sourceRoot = options.buildRoot ? resolve(options.buildRoot) : buildRoot;
  const outputPath = options.outputPath ? resolve(options.outputPath) : externalPayloadPath;
  const entryPath = join(sourceRoot, 'index.html');
  if (!fs.existsSync(entryPath)) {
    throw new Error(`Hosted WebConfig export is missing: ${entryPath}`);
  }

  const manifest = createAssetManifestFromHtml(fs.readFileSync(entryPath, 'utf8'));
  const files = [...manifest]
    .map((path) => ({ path, absolute: join(sourceRoot, path) }))
    .filter(({ absolute }) => fs.existsSync(absolute) && fs.statSync(absolute).isFile())
    .sort((left, right) => left.path.localeCompare(right.path));
  if (!files.some(({ path }) => path === 'index.html')) {
    throw new Error('Hosted WebConfig export does not contain index.html');
  }

  const payloads = files.map(({ path, absolute }) => {
    const qualifiedName = `/${path}`;
    const terminatedLength = Buffer.byteLength(qualifiedName, 'utf8') + 1;
    const paddedLength = Math.ceil(terminatedLength / 4) * 4;
    const paddedName = qualifiedName + '\0'.repeat(paddedLength - Buffer.byteLength(qualifiedName, 'utf8'));
    return makeFileBuffer(
      paddedName,
      normalizeExtension(path),
      false,
      fs.readFileSync(absolute),
    );
  });

  const header = Buffer.alloc((payloads.length + 1) * 4);
  header.writeUInt32BE(payloads.length, 0);
  payloads.forEach((payload, index) => header.writeUInt32BE(payload.byteLength, (index + 1) * 4));
  const output = Buffer.concat([header, ...payloads]);
  const budget = assertWebResourceBudget(output.byteLength);
  fs.mkdirSync(dirname(outputPath), { recursive: true });
  fs.writeFileSync(outputPath, output);
  return { outputPath, files: files.map(({ path }) => path), ...budget };
}

function normalizeExtension(value) {
  const base = String(value).split(/[?#]/, 1)[0];
  const extension = base.includes('.') ? base.slice(base.lastIndexOf('.') + 1) : base;
  return extension.toLowerCase().replace(/^\./, '');
}

if (process.argv[1] && resolve(process.argv[1]) === resolve(scriptPath)) {
  const result = buildExternalWebResources();
  console.log(
    `Generated immutable WebResources payload: ${result.outputPath} ` +
    `(${result.byteLength} bytes, ${result.files.length} files)`,
  );
}
