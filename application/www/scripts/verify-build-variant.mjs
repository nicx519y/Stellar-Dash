import { existsSync, readFileSync, readdirSync, statSync } from 'node:fs';
import path from 'node:path';

const variant = process.argv[2];
if (variant !== 'hosted' && variant !== 'mock') {
  throw new Error('Usage: node scripts/verify-build-variant.mjs <hosted|mock>');
}

const buildRoot = path.resolve(process.cwd(), variant === 'mock' ? 'build-mock' : 'build');
const mockMarkers = [
  'HBOX-V2-MOCK-0001',
  'mock-session',
  'HBox V2 Mock Device',
  'MOCK DEVICE',
];
const requiredMockMarkers = [
  'HBOX-V2-MOCK-0001',
  'MOCK DEVICE',
];

if (!existsSync(buildRoot)) {
  throw new Error(`${variant} build output does not exist: ${buildRoot}`);
}

const artifactFiles = walk(buildRoot)
  .filter((file) => file.endsWith('.html') || file.endsWith('.js'));
const htmlFiles = artifactFiles.filter((file) => file.endsWith('.html'));
const referencedChunks = new Set();
const markerHits = new Map(mockMarkers.map((marker) => [marker, []]));
const violations = [];

for (const file of artifactFiles) {
  const source = readFileSync(file, 'utf8');
  for (const marker of mockMarkers) {
    if (source.includes(marker)) {
      markerHits.get(marker).push(path.relative(buildRoot, file));
    }
  }
  if (file.endsWith('.html')) {
    for (const match of source.matchAll(/(?:src|href)=["']([^"']+\.js(?:\?[^"']*)?)["']/g)) {
      const relative = match[1].replace(/[?#].*$/, '').replace(/^\//, '');
      if (relative.startsWith('_next/static/')) {
        referencedChunks.add(relative);
      }
    }
  }
}

if (referencedChunks.size === 0) {
  violations.push('no JavaScript chunks are referenced by exported HTML');
}
for (const relative of referencedChunks) {
  if (!existsSync(path.join(buildRoot, relative))) {
    violations.push(`exported HTML references missing chunk ${relative}`);
  }
}

if (variant === 'hosted') {
  for (const [marker, files] of markerHits) {
    for (const file of files) {
      violations.push(`${file} contains forbidden ${JSON.stringify(marker)}`);
    }
  }
} else {
  for (const marker of requiredMockMarkers) {
    if (markerHits.get(marker).length === 0) {
      violations.push(`Mock build is missing required ${JSON.stringify(marker)}`);
    }
  }
}

if (violations.length > 0) {
  throw new Error(`${variant} build isolation failed:\n${violations.join('\n')}`);
}

console.log(
  `${variant} build isolation passed: ${htmlFiles.length} HTML files, ` +
  `${referencedChunks.size} referenced chunks, ${artifactFiles.length} artifacts scanned.`,
);

function walk(root) {
  const files = [];
  for (const entry of readdirSync(root)) {
    const target = path.join(root, entry);
    if (statSync(target).isDirectory()) {
      files.push(...walk(target));
    } else {
      files.push(target);
    }
  }
  return files;
}
