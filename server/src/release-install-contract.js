'use strict';
const crypto = require('node:crypto');

const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const integer = value => Number.isSafeInteger(value) && value >= 0 && value <= 0xffffffff;
function validateInstallContract(manifest) {
    const fail = message => { throw new Error(`Invalid installation contract: ${message}`); };
    const range = (r, name) => {
        if (!r || !integer(r.min) || !integer(r.max) || r.min > r.max) fail(name);
    };
    if (manifest.schemaVersion !== 2) return;
    const c = manifest.install;
    if (!c || c.protocol !== 1 || c.order !== 'tx-then-stm32' ||
        typeof manifest.buildId !== 'string' || !/^[A-Za-z0-9._-]{1,64}$/.test(manifest.buildId)) fail('protocol/order/buildId');
    range(c.configRead, 'configRead');
    if (!integer(c.configWrite) || c.configWrite < c.configRead.min || c.configWrite > c.configRead.max) fail('configWrite');
    range(c.stm32Maintenance, 'stm32Maintenance');
    range(c.txMaintenance, 'txMaintenance');
    if (c.stm32Maintenance.min > 1 || c.stm32Maintenance.max < 1 ||
        c.txMaintenance.min > 1 || c.txMaintenance.max < 1) fail('unsupported maintenance protocol');
    for (const a of manifest.artifacts) {
        if (!/^[A-Za-z0-9._-]{1,64}$/.test(a.buildId) || a.buildId === 'unidentified') fail('artifact buildId');
        if (a.component === 'stm32' && !/^[a-f0-9]{64}$/.test(a.metadataSha256 || '')) fail('metadataSha256');
        if (a.component === 'tx') {
            if (a.applicationOffset !== 4096 || a.applicationSize !== a.size - 4096 ||
                a.applicationSize > 0x6f000 || !/^[a-f0-9]{64}$/.test(a.applicationSha256 || '')) fail('TX application boundary/digest');
        }
    }
    if (Buffer.byteLength(JSON.stringify(manifest)) > 8192) fail('manifest exceeds 8 KiB');
}

function validateInstallArtifact(artifact, bytes, readZip, configVersion) {
    let executable = bytes.subarray(4096);
    if (artifact.component === 'stm32') {
        const entries = readZip(bytes);
        const metadata = entries.get('metadata.bin');
        if (!metadata || hash(metadata) !== artifact.metadataSha256) throw new Error('STM32 metadata binding mismatch');
        const inner = JSON.parse(entries.get('manifest.json').toString('utf8'));
        executable = entries.get(inner.components.find(c => c.name === 'application').file);
    } else if (artifact.component === 'tx') {
        const app = bytes.subarray(4096);
        if (hash(app) !== artifact.applicationSha256) throw new Error('TX application digest mismatch');
    }
    const expectedComponent = artifact.component === 'stm32' ? 1 : 2;
    const magic = Buffer.from('XORAFW2\0');
    let matches = 0;
    for (let offset = executable.indexOf(magic); offset >= 0; offset = executable.indexOf(magic, offset + 1)) {
        if (offset + 121 > executable.length) continue;
        const record = executable.subarray(offset, offset + 121);
        const fixed = (start, size) => {
            const value = record.subarray(start, start + size); const end = value.indexOf(0);
            return end < 0 ? '' : value.subarray(0, end).toString('ascii');
        };
        if (record.readUInt32LE(8) === expectedComponent && record.readUInt32LE(12) === 1 &&
            record.readUInt32LE(16) === 1 &&
            (artifact.component !== 'stm32' || record.readUInt32LE(20) === configVersion) &&
            fixed(24, 32) === artifact.version && fixed(56, 65) === artifact.buildId) matches++;
    }
    if (matches !== 1) throw new Error('Executable build identity does not match signed release declaration');
}

module.exports = { validateInstallContract, validateInstallArtifact };
