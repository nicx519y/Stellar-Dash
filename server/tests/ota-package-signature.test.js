'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const test = require('node:test');

const { validateUploadedOtaPackage } = require('../src/action');

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

function makeSignedPackage() {
    const releaseKeys = crypto.generateKeyPairSync('ec', {
        namedCurve: 'prime256v1'
    });
    const files = new Map([
        ['application.bin', Buffer.from('application fixture')],
        ['adc_mapping.bin', Buffer.from('adc mapping fixture')]
    ]);
    const manifest = {
        version: '1.2.3',
        slot: 'A',
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
    const metadata = Buffer.alloc(METADATA_SIZE);
    metadata.writeUInt32LE(0x48424f58, 0);
    metadata.writeUInt32LE(1, 4);
    metadata.writeUInt32LE(0, 8);
    metadata.writeUInt32LE(METADATA_SIZE, 12);
    writeFixed(metadata, 20, 32, manifest.version);
    metadata[52] = 0;
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

function makeStoredZip(entries) {
    const localParts = [];
    const centralParts = [];
    let localOffset = 0;
    for (const [name, source] of entries) {
        const fileName = Buffer.from(name, 'utf8');
        const data = Buffer.from(source);
        const checksum = crc32(data);
        const local = Buffer.alloc(30);
        local.writeUInt32LE(0x04034b50, 0);
        local.writeUInt16LE(20, 4);
        local.writeUInt32LE(checksum, 14);
        local.writeUInt32LE(data.length, 18);
        local.writeUInt32LE(data.length, 22);
        local.writeUInt16LE(fileName.length, 26);
        localParts.push(local, fileName, data);

        const central = Buffer.alloc(46);
        central.writeUInt32LE(0x02014b50, 0);
        central.writeUInt16LE(20, 4);
        central.writeUInt16LE(20, 6);
        central.writeUInt32LE(checksum, 16);
        central.writeUInt32LE(data.length, 20);
        central.writeUInt32LE(data.length, 24);
        central.writeUInt16LE(fileName.length, 28);
        central.writeUInt32LE(localOffset, 42);
        centralParts.push(central, fileName);
        localOffset += local.length + fileName.length + data.length;
    }
    const centralDirectory = Buffer.concat(centralParts);
    const end = Buffer.alloc(22);
    end.writeUInt32LE(0x06054b50, 0);
    end.writeUInt16LE(entries.length, 8);
    end.writeUInt16LE(entries.length, 10);
    end.writeUInt32LE(centralDirectory.length, 12);
    end.writeUInt32LE(localOffset, 16);
    return Buffer.concat([...localParts, centralDirectory, end]);
}

function packageEntries(fixture) {
    return [
        ['manifest.json', Buffer.from(JSON.stringify(fixture.manifest))],
        ['metadata.bin', fixture.metadata],
        ...fixture.files
    ];
}

function writePackage(t, entries) {
    const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'hbox-ota-'));
    t.after(() => fs.rmSync(directory, { recursive: true, force: true }));
    const packagePath = path.join(directory, 'release.zip');
    fs.writeFileSync(packagePath, makeStoredZip(entries));
    return packagePath;
}

test('OTA upload accepts exact signed metadata and optional hosted webresources', t => {
    const fixture = makeSignedPackage();
    const packagePath = writePackage(t, packageEntries(fixture));
    const manifest = validateUploadedOtaPackage(
        packagePath,
        'A',
        fixture.releaseKeys.publicKey
    );
    assert.equal(manifest.metadata.file, 'metadata.bin');
    assert.equal(manifest.components[1].active, false);
});

test('OTA upload rejects missing metadata and a missing release trust key', t => {
    const fixture = makeSignedPackage();
    const withoutMetadata = packageEntries(fixture)
        .filter(([name]) => name !== 'metadata.bin');
    assert.throws(
        () => validateUploadedOtaPackage(
            writePackage(t, withoutMetadata),
            'A',
            fixture.releaseKeys.publicKey
        ),
        /metadata\.bin ZIP content size mismatch/
    );
    assert.throws(
        () => validateUploadedOtaPackage(
            writePackage(t, packageEntries(fixture)),
            'A'
        ),
        /release public key is not configured/
    );
});

test('OTA upload rejects metadata whose matching raw signature was tampered', t => {
    const fixture = makeSignedPackage();
    fixture.metadata[SIGNATURE_OFFSET] ^= 0x01;
    fixture.manifest.signature = fixture.metadata
        .subarray(SIGNATURE_OFFSET, SIGNATURE_OFFSET + 64)
        .toString('hex');
    fixture.metadata.writeUInt32LE(
        crc32(fixture.metadata, CRC_OFFSET, 4),
        CRC_OFFSET
    );
    fixture.manifest.metadata.sha256 = crypto.createHash('sha256')
        .update(fixture.metadata)
        .digest('hex');
    assert.throws(
        () => validateUploadedOtaPackage(
            writePackage(t, packageEntries(fixture)),
            'A',
            fixture.releaseKeys.publicKey
        ),
        /release signature is invalid/
    );
});
