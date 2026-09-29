#!/usr/bin/env node
'use strict';

// Offline packaging only: no server, device, signing key upload or Flash operation.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const os = require('node:os');
const { validateBundle, validateManifest } = require('../src/firmware-releases');
const { readFlatZipEntries } = require('../src/action');

function crc32(data) {
    let c = 0xffffffff;
    for (const b of data) { c ^= b; for (let n = 0; n < 8; n++) c = (c >>> 1) ^ ((c & 1) ? 0xedb88320 : 0); }
    return (c ^ 0xffffffff) >>> 0;
}
function storedZip(entries) {
    const local = []; const central = []; let offset = 0;
    for (const [name, data] of entries) {
        const n = Buffer.from(name); const crc = crc32(data);
        const l = Buffer.alloc(30); l.writeUInt32LE(0x04034b50); l.writeUInt16LE(20, 4);
        l.writeUInt32LE(crc, 14); l.writeUInt32LE(data.length, 18); l.writeUInt32LE(data.length, 22); l.writeUInt16LE(n.length, 26);
        local.push(l, n, data);
        const c = Buffer.alloc(46); c.writeUInt32LE(0x02014b50); c.writeUInt16LE(20, 4); c.writeUInt16LE(20, 6);
        c.writeUInt32LE(crc, 16); c.writeUInt32LE(data.length, 20); c.writeUInt32LE(data.length, 24); c.writeUInt16LE(n.length, 28); c.writeUInt32LE(offset, 42);
        central.push(c, n); offset += l.length + n.length + data.length;
    }
    const directory = Buffer.concat(central); const end = Buffer.alloc(22);
    end.writeUInt32LE(0x06054b50); end.writeUInt16LE(entries.length, 8); end.writeUInt16LE(entries.length, 10);
    end.writeUInt32LE(directory.length, 12); end.writeUInt32LE(offset, 16);
    return Buffer.concat([...local, directory, end]);
}
function createBundle(manifestPath, keyPath, outputPath) {
    const manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8'));
    const entries = manifest.artifacts.map(a => {
        if (!/^[A-Za-z0-9][A-Za-z0-9_.-]{0,127}$/.test(a.file)) throw new Error('Artifact files must be flat filenames next to the manifest');
        const data = fs.readFileSync(path.join(path.dirname(manifestPath), a.file));
        a.size = data.length; a.sha256 = crypto.createHash('sha256').update(data).digest('hex');
        if (manifest.schemaVersion === 2 && a.component === 'stm32') {
            const metadata = readFlatZipEntries(path.join(path.dirname(manifestPath), a.file)).get('metadata.bin');
            if (!metadata) throw new Error('Missing signed metadata.bin');
            a.metadataSha256 = crypto.createHash('sha256').update(metadata).digest('hex');
        }
        if (manifest.schemaVersion === 2 && a.component === 'tx') {
            a.applicationOffset = 4096; a.applicationSize = data.length - 4096;
            a.applicationSha256 = crypto.createHash('sha256').update(data.subarray(4096)).digest('hex');
        }
        return [a.file, data];
    });
    validateManifest(manifest);
    const key = crypto.createPrivateKey(fs.readFileSync(keyPath));
    if (key.asymmetricKeyDetails?.namedCurve !== 'prime256v1') throw new Error('A P-256 signing key is required');
    const raw = Buffer.from(JSON.stringify(manifest));
    const signature = crypto.sign('sha256', raw, { key, dsaEncoding: 'ieee-p1363' });
    const zip = storedZip([['release.json', raw], ['release.sig', signature], ...entries]);
    const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'xora-release-'));
    try {
        const candidate = path.join(tmp, 'bundle.zip'); fs.writeFileSync(candidate, zip);
        validateBundle(candidate, crypto.createPublicKey(key), tmp);
        fs.writeFileSync(outputPath, zip, { flag: 'wx' });
    } finally { fs.rmSync(tmp, { recursive: true, force: true }); }
}
if (require.main === module) {
    const [manifest, key, output] = process.argv.slice(2);
    if (!manifest || !key || !output || process.argv.length !== 5) {
        console.error('Usage: node server/scripts/create-firmware-bundle.js release-source.json signing-key.pem release.zip');
        process.exitCode = 1;
    } else {
        try { createBundle(manifest, key, output); console.log('XORA release package validated and written. Nothing was uploaded.'); }
        catch (error) { console.error(error.message); process.exitCode = 1; }
    }
}
module.exports = { createBundle, storedZip };
