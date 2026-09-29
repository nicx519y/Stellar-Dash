'use strict';
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const METADATA_SIZE = 807;
const CRC_OFFSET = 16;
const HASH_OFFSET = 643;
const SIGNATURE_OFFSET = 675;

function crc32(data, skipOffset = data.length, skipSize = 0) {
    let crc = 0xffffffff;
    for (let index = 0; index < data.length; index += 1) {
        if (index >= skipOffset && index < skipOffset + skipSize) continue;
        crc ^= data[index];
        for (let bit = 0; bit < 8; bit += 1) {
            crc = (crc >>> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
        }
    }
    return (crc ^ 0xffffffff) >>> 0;
}

function writeFixed(buffer, offset, width, value) {
    const encoded = Buffer.from(value, 'utf8');
    assert.ok(encoded.length < width);
    encoded.copy(buffer, offset);
}

function canonicalMetadata(metadata) {
    const canonical = Buffer.from(metadata);
    canonical.fill(0, CRC_OFFSET, CRC_OFFSET + 4);
    canonical.fill(0, HASH_OFFSET, HASH_OFFSET + 32);
    canonical.fill(0, SIGNATURE_OFFSET, SIGNATURE_OFFSET + 64);
    return canonical;
}

function makeSignedPackage(slot = "A", keys = null, options = {}) {
    const releaseKeys = keys || crypto.generateKeyPairSync('ec', {
        namedCurve: 'prime256v1'
    });
    const files = new Map([
        ['application.bin', options.application || Buffer.from('application fixture')],
        ['adc_mapping.bin', Buffer.from('adc mapping fixture')]
    ]);
    const manifest = {
        version: '1.2.3',
        slot,
        build_date: '2026-08-05 12:00:00',
        build_timestamp: 1785921600,
        hardware_version: '2.0.0',
        hardware_version_code: 0x00020000,
        ota_scope: 'STM32_ONLY',
        ch585_update: 'MANUAL_INDEPENDENT_FLASH',
        security_version: 1,
        webresources_optional: true,
        trust_bundle_sha256: 'ab'.repeat(32),
        components: [
            {
                name: 'application',
                file: 'application.bin',
                address: '0x90000000',
                size: files.get('application.bin').length,
                sha256: crypto.createHash('sha256')
                    .update(files.get('application.bin')).digest('hex'),
                file_type: 'bin',
                active: true
            },
            {
                name: 'webresources',
                file: '',
                address: '0x90100000',
                size: 0,
                sha256: '0'.repeat(64),
                file_type: 'none',
                active: false
            },
            {
                name: 'adc_mapping',
                file: 'adc_mapping.bin',
                address: '0x90280000',
                size: files.get('adc_mapping.bin').length,
                sha256: crypto.createHash('sha256')
                    .update(files.get('adc_mapping.bin')).digest('hex'),
                file_type: 'bin',
                active: true
            }
        ]
    };
    if (slot === 'B') { for (const c of manifest.components) c.address = '0x' + (Number(c.address) + 0x2b0000).toString(16); }
    const metadata = Buffer.alloc(METADATA_SIZE);
    metadata.writeUInt32LE(0x48424f58, 0);
    metadata.writeUInt32LE(1, 4);
    metadata.writeUInt32LE(0, 8);
    metadata.writeUInt32LE(METADATA_SIZE, 12);
    writeFixed(metadata, 20, 32, manifest.version);
    metadata[52] = slot === 'A' ? 0 : 1;
    writeFixed(metadata, 53, 32, manifest.build_date);
    metadata.writeUInt32LE(manifest.build_timestamp, 85);
    writeFixed(metadata, 89, 32, 'STM32H750_HBOX');
    metadata.writeUInt32LE(manifest.hardware_version_code, 121);
    metadata.writeUInt32LE(0x00010000, 125);
    metadata.writeUInt32LE(3, 129);
    manifest.components.forEach((component, index) => {
        const base = 133 + index * 170;
        writeFixed(metadata, base, 32, component.name);
        writeFixed(metadata, base + 32, 64, component.file);
        metadata.writeUInt32LE(Number(component.address), base + 96);
        metadata.writeUInt32LE(component.size, base + 100);
        writeFixed(metadata, base + 104, 65, component.sha256);
        metadata[base + 169] = component.active ? 1 : 0;
    });
    metadata.writeUInt32LE(1, 739);
    metadata.writeUInt32LE(manifest.security_version, 743);
    metadata[747] = 1;

    const canonical = canonicalMetadata(metadata);
    const firmwareHash = crypto.createHash('sha256').update(canonical).digest();
    const signature = crypto.sign(
        'sha256',
        canonical,
        { key: releaseKeys.privateKey, dsaEncoding: 'ieee-p1363' }
    );
    firmwareHash.copy(metadata, HASH_OFFSET);
    signature.copy(metadata, SIGNATURE_OFFSET);
    metadata.writeUInt32LE(crc32(metadata, CRC_OFFSET, 4), CRC_OFFSET);
    manifest.signature_algorithm = 1;
    manifest.firmware_hash = firmwareHash.toString('hex');
    manifest.signature = signature.toString('hex');
    manifest.metadata = {
        file: 'metadata.bin',
        size: metadata.length,
        sha256: crypto.createHash('sha256').update(metadata).digest('hex')
    };
    return { releaseKeys, manifest, metadata, files };
}


module.exports = { makeSignedPackage };
