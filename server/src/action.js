#!/usr/bin/env node
// -*- coding: utf-8 -*-

/**
 * 网络接口入口模块
 * 
 * 功能:
 * 1. 统一管理所有HTTP路由
 * 2. 健康检查接口
 * 3. 设备管理接口
 * 4. 管理员认证接口
 * 5. 固件管理接口
 */

const express = require('express');
const multer = require('multer');
const path = require('path');
const crypto = require('crypto');
const fs = require('fs-extra');
const zlib = require('zlib');
const { createLegacyDownloadUrl } = require('./download-access');

const STM32_OTA_COMPONENTS = Object.freeze([
    'application',
    'webresources',
    'adc_mapping'
]);
const STM32_OTA_LAYOUT = Object.freeze({
    A: Object.freeze({
        application: Object.freeze({ address: 0x90000000, maxSize: 0x100000 }),
        webresources: Object.freeze({ address: 0x90100000, maxSize: 0x180000 }),
        adc_mapping: Object.freeze({ address: 0x90280000, maxSize: 0x20000 })
    }),
    B: Object.freeze({
        application: Object.freeze({ address: 0x902B0000, maxSize: 0x100000 }),
        webresources: Object.freeze({ address: 0x903B0000, maxSize: 0x180000 }),
        adc_mapping: Object.freeze({ address: 0x90530000, maxSize: 0x20000 })
    })
});
const MAX_OTA_ENTRY_SIZE = 0x180000;
const MAX_OTA_UNCOMPRESSED_SIZE = 0x2B0000 + 0x10000;
const SIGNED_METADATA_FILENAME = 'metadata.bin';
const METADATA_STRUCT_SIZE = 807;
const METADATA_CRC32_OFFSET = 16;
const FIRMWARE_HASH_OFFSET = 643;
const FIRMWARE_SIGNATURE_OFFSET = 675;
const FIRMWARE_SIGNATURE_ALGORITHM_OFFSET = 739;
const FIRMWARE_SECURITY_VERSION_OFFSET = 743;
const FIRMWARE_WEBRESOURCES_OPTIONAL_OFFSET = 747;
const COMPONENT_STRUCT_SIZE = 170;
const FIRMWARE_MAGIC = 0x48424F58;
const METADATA_VERSION_MAJOR = 1;
const METADATA_VERSION_MINOR = 0;
const FIRMWARE_SIGNATURE_ECDSA_P256_SHA256 = 1;
const MINIMUM_FIRMWARE_SECURITY_VERSION = 1;
const BOOTLOADER_VERSION = 0x00010000;
const HARDWARE_VERSION_CODE_V2 = 0x00020000;
const DEVICE_MODEL = 'STM32H750_HBOX';

function hardwareVersionCode(hardwareVersion) {
    if (typeof hardwareVersion !== 'string') {
        return null;
    }
    const match = /^(\d+)\.(\d+)\.(\d+)$/.exec(hardwareVersion);
    if (!match) {
        return null;
    }
    const parts = match.slice(1).map(Number);
    if (parts.some(part => !Number.isInteger(part) || part < 0 || part > 255)) {
        return null;
    }
    return ((parts[0] << 16) | (parts[1] << 8) | parts[2]) >>> 0;
}

function parseManifestAddress(value, fieldName) {
    let parsed;
    if (Number.isInteger(value)) {
        parsed = value;
    } else if (typeof value === 'string' && /^(?:0x[0-9a-f]+|\d+)$/i.test(value)) {
        parsed = Number.parseInt(value, value.toLowerCase().startsWith('0x') ? 16 : 10);
    }
    if (!Number.isSafeInteger(parsed) || parsed < 0) {
        throw new Error(`${fieldName} is not a valid address`);
    }
    return parsed;
}

/*
 * Read the classic ZIP central directory with Node built-ins. Release packages
 * are intentionally small, flat archives; ZIP64, encryption and nested paths
 * are rejected so upload validation remains deterministic and fail-closed.
 */
function readFlatZipEntries(filePath) {
    const archive = fs.readFileSync(filePath);
    const eocdSignature = 0x06054b50;
    const searchStart = Math.max(0, archive.length - 65557);
    let eocdOffset = -1;
    for (let offset = archive.length - 22; offset >= searchStart; offset -= 1) {
        if (archive.readUInt32LE(offset) === eocdSignature) {
            eocdOffset = offset;
            break;
        }
    }
    if (eocdOffset < 0) {
        throw new Error('invalid ZIP: EOCD not found');
    }
    if (archive.readUInt16LE(eocdOffset + 4) !== 0 ||
        archive.readUInt16LE(eocdOffset + 6) !== 0) {
        throw new Error('multi-disk ZIP is not supported');
    }
    const entryCount = archive.readUInt16LE(eocdOffset + 10);
    const centralSize = archive.readUInt32LE(eocdOffset + 12);
    const centralOffset = archive.readUInt32LE(eocdOffset + 16);
    if (entryCount === 0xffff || centralSize === 0xffffffff ||
        centralOffset === 0xffffffff ||
        centralOffset + centralSize > eocdOffset) {
        throw new Error('ZIP64 or invalid central directory is not supported');
    }

    const entries = new Map();
    let totalUncompressedSize = 0;
    let offset = centralOffset;
    for (let index = 0; index < entryCount; index += 1) {
        if (offset + 46 > archive.length || archive.readUInt32LE(offset) !== 0x02014b50) {
            throw new Error('invalid ZIP central directory entry');
        }
        const flags = archive.readUInt16LE(offset + 8);
        const method = archive.readUInt16LE(offset + 10);
        const compressedSize = archive.readUInt32LE(offset + 20);
        const uncompressedSize = archive.readUInt32LE(offset + 24);
        const nameLength = archive.readUInt16LE(offset + 28);
        const extraLength = archive.readUInt16LE(offset + 30);
        const commentLength = archive.readUInt16LE(offset + 32);
        const localOffset = archive.readUInt32LE(offset + 42);
        const entryEnd = offset + 46 + nameLength + extraLength + commentLength;
        if (entryEnd > archive.length) {
            throw new Error('truncated ZIP central directory entry');
        }
        const name = archive.subarray(offset + 46, offset + 46 + nameLength).toString('utf8');
        if (!name || name.includes('/') || name.includes('\\') || entries.has(name)) {
            throw new Error(`invalid or duplicate ZIP entry: ${name}`);
        }
        if ((flags & 0x0001) !== 0) {
            throw new Error(`encrypted ZIP entry is not allowed: ${name}`);
        }
        if (method !== 0 && method !== 8) {
            throw new Error(`unsupported ZIP compression method for ${name}`);
        }
        totalUncompressedSize += uncompressedSize;
        if (uncompressedSize > MAX_OTA_ENTRY_SIZE ||
            totalUncompressedSize > MAX_OTA_UNCOMPRESSED_SIZE) {
            throw new Error('ZIP uncompressed size exceeds the STM32 OTA boundary');
        }
        if (localOffset + 30 > archive.length ||
            archive.readUInt32LE(localOffset) !== 0x04034b50) {
            throw new Error(`invalid ZIP local header for ${name}`);
        }
        const localFlags = archive.readUInt16LE(localOffset + 6);
        const localMethod = archive.readUInt16LE(localOffset + 8);
        const localNameLength = archive.readUInt16LE(localOffset + 26);
        const localExtraLength = archive.readUInt16LE(localOffset + 28);
        const localName = archive.subarray(
            localOffset + 30,
            localOffset + 30 + localNameLength
        ).toString('utf8');
        if (localFlags !== flags || localMethod !== method || localName !== name) {
            throw new Error(`ZIP local/central header mismatch for ${name}`);
        }
        const dataOffset = localOffset + 30 + localNameLength + localExtraLength;
        const dataEnd = dataOffset + compressedSize;
        if (dataEnd > archive.length) {
            throw new Error(`truncated ZIP entry: ${name}`);
        }
        const compressed = archive.subarray(dataOffset, dataEnd);
        const data = method === 0
            ? Buffer.from(compressed)
            : zlib.inflateRawSync(compressed, {
                // Do not trust the central-directory size before inflation.
                // Bound output independently so a forged DEFLATE stream cannot
                // turn a small upload into a server-side memory bomb.
                maxOutputLength: Math.min(
                    MAX_OTA_ENTRY_SIZE,
                    uncompressedSize
                ) + 1
            });
        if (data.length !== uncompressedSize) {
            throw new Error(`ZIP size mismatch for ${name}`);
        }
        entries.set(name, data);
        offset = entryEnd;
    }
    if (offset !== centralOffset + centralSize) {
        throw new Error('ZIP central directory size mismatch');
    }
    return entries;
}

function crc32Skipping(data, skipOffset, skipSize) {
    let crc = 0xffffffff;
    for (let index = 0; index < data.length; index += 1) {
        if (index >= skipOffset && index < skipOffset + skipSize) {
            continue;
        }
        crc ^= data[index];
        for (let bit = 0; bit < 8; bit += 1) {
            crc = (crc >>> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
        }
    }
    return (crc ^ 0xffffffff) >>> 0;
}

function canonicalMetadata(metadata) {
    const canonical = Buffer.from(metadata);
    canonical.fill(0, METADATA_CRC32_OFFSET, METADATA_CRC32_OFFSET + 4);
    canonical.fill(0, FIRMWARE_HASH_OFFSET, FIRMWARE_HASH_OFFSET + 32);
    canonical.fill(
        0,
        FIRMWARE_SIGNATURE_OFFSET,
        FIRMWARE_SIGNATURE_OFFSET + 64
    );
    return canonical;
}

function decodeFixedMetadataText(metadata, offset, width, fieldName) {
    const encoded = metadata.subarray(offset, offset + width);
    const terminator = encoded.indexOf(0);
    const content = terminator >= 0
        ? encoded.subarray(0, terminator)
        : encoded;
    if (terminator >= 0 &&
        encoded.subarray(terminator + 1).some(value => value !== 0)) {
        throw new Error(`metadata.bin ${fieldName} has non-canonical padding`);
    }
    const decoded = content.toString('utf8');
    if (!Buffer.from(decoded, 'utf8').equals(content)) {
        throw new Error(`metadata.bin ${fieldName} is not valid UTF-8`);
    }
    return decoded;
}

function exactBufferEquals(left, right) {
    return left.length === right.length && crypto.timingSafeEqual(left, right);
}

function validateSignedMetadata(metadata, manifest, releasePublicKey) {
    if (!Buffer.isBuffer(metadata) || metadata.length !== METADATA_STRUCT_SIZE) {
        throw new Error(
            `metadata.bin must be exactly ${METADATA_STRUCT_SIZE} bytes`
        );
    }
    if (metadata.readUInt32LE(0) !== FIRMWARE_MAGIC ||
        metadata.readUInt32LE(4) !== METADATA_VERSION_MAJOR ||
        metadata.readUInt32LE(8) !== METADATA_VERSION_MINOR ||
        metadata.readUInt32LE(12) !== METADATA_STRUCT_SIZE) {
        throw new Error('metadata.bin header is invalid');
    }
    const storedCrc = metadata.readUInt32LE(METADATA_CRC32_OFFSET);
    const expectedCrc = crc32Skipping(metadata, METADATA_CRC32_OFFSET, 4);
    if (storedCrc === 0 || storedCrc !== expectedCrc) {
        throw new Error('metadata.bin CRC32 is invalid');
    }

    if (decodeFixedMetadataText(metadata, 20, 32, 'firmware_version') !==
        manifest.version ||
        metadata[52] !== (manifest.slot === 'A' ? 0 : 1) ||
        decodeFixedMetadataText(metadata, 53, 32, 'build_date') !==
            manifest.build_date ||
        metadata.readUInt32LE(85) !== manifest.build_timestamp ||
        decodeFixedMetadataText(metadata, 89, 32, 'device_model') !==
            DEVICE_MODEL ||
        metadata.readUInt32LE(121) !== manifest.hardware_version_code ||
        metadata.readUInt32LE(125) !== BOOTLOADER_VERSION ||
        metadata.readUInt32LE(129) !== STM32_OTA_COMPONENTS.length) {
        throw new Error('metadata.bin header does not match manifest.json');
    }

    for (let index = 0; index < manifest.components.length; index += 1) {
        const component = manifest.components[index];
        const base = 133 + index * COMPONENT_STRUCT_SIZE;
        if (decodeFixedMetadataText(
            metadata, base, 32, `components[${index}].name`
        ) !== component.name ||
            decodeFixedMetadataText(
                metadata, base + 32, 64, `components[${index}].file`
            ) !== component.file ||
            metadata.readUInt32LE(base + 96) !== parseManifestAddress(
                component.address, `${component.name}.address`
            ) ||
            metadata.readUInt32LE(base + 100) !== component.size ||
            decodeFixedMetadataText(
                metadata, base + 104, 65, `components[${index}].sha256`
            ) !== component.sha256 ||
            metadata[base + 169] !== (component.active === false ? 0 : 1)) {
            throw new Error(
                `metadata.bin component ${component.name} does not match manifest.json`
            );
        }
    }

    const canonical = canonicalMetadata(metadata);
    const expectedFirmwareHash = crypto.createHash('sha256')
        .update(canonical)
        .digest();
    const embeddedFirmwareHash = metadata.subarray(
        FIRMWARE_HASH_OFFSET,
        FIRMWARE_HASH_OFFSET + 32
    );
    const manifestFirmwareHash = Buffer.from(manifest.firmware_hash, 'hex');
    if (!exactBufferEquals(embeddedFirmwareHash, expectedFirmwareHash) ||
        !exactBufferEquals(embeddedFirmwareHash, manifestFirmwareHash)) {
        throw new Error('metadata.bin firmware_hash is invalid');
    }
    const embeddedSignature = metadata.subarray(
        FIRMWARE_SIGNATURE_OFFSET,
        FIRMWARE_SIGNATURE_OFFSET + 64
    );
    if (!exactBufferEquals(
        embeddedSignature,
        Buffer.from(manifest.signature, 'hex')
    ) ||
        metadata.readUInt32LE(FIRMWARE_SIGNATURE_ALGORITHM_OFFSET) !==
            manifest.signature_algorithm ||
        metadata.readUInt32LE(FIRMWARE_SECURITY_VERSION_OFFSET) !==
            manifest.security_version ||
        metadata[FIRMWARE_WEBRESOURCES_OPTIONAL_OFFSET] !==
            Number(manifest.webresources_optional) ||
        metadata.subarray(FIRMWARE_WEBRESOURCES_OPTIONAL_OFFSET + 1)
            .some(value => value !== 0)) {
        throw new Error('metadata.bin security policy does not match manifest.json');
    }

    if (!releasePublicKey) {
        throw new Error('firmware release public key is not configured');
    }
    let publicKey;
    try {
        publicKey = releasePublicKey.type === 'public'
            ? releasePublicKey
            : crypto.createPublicKey(releasePublicKey);
    } catch (error) {
        throw new Error(`firmware release public key is invalid: ${error.message}`);
    }
    if (publicKey.asymmetricKeyType !== 'ec' ||
        publicKey.asymmetricKeyDetails?.namedCurve !== 'prime256v1' ||
        !crypto.verify(
            'sha256',
            canonical,
            { key: publicKey, dsaEncoding: 'ieee-p1363' },
            embeddedSignature
        )) {
        throw new Error('metadata.bin release signature is invalid');
    }
}

function validateUploadedOtaPackage(filePath, expectedSlot, releasePublicKey) {
    const entries = readFlatZipEntries(filePath);
    const manifestData = entries.get('manifest.json');
    if (!manifestData) {
        throw new Error('manifest.json is required');
    }
    let manifest;
    try {
        manifest = JSON.parse(manifestData.toString('utf8'));
    } catch (error) {
        throw new Error(`invalid manifest.json: ${error.message}`);
    }
    if (!manifest || typeof manifest !== 'object' || Array.isArray(manifest)) {
        throw new Error('manifest root must be an object');
    }
    if (!isValidVersion(manifest.version)) {
        throw new Error('manifest.version must be a three-part version');
    }
    if (Buffer.byteLength(manifest.version, 'utf8') > 31) {
        throw new Error('manifest.version is too long for metadata.bin');
    }
    const slot = String(manifest.slot || '').toUpperCase();
    if (manifest.slot !== expectedSlot ||
        slot !== expectedSlot ||
        !STM32_OTA_LAYOUT[slot]) {
        throw new Error(`manifest.slot must be ${expectedSlot}`);
    }
    const expectedHardwareCode = hardwareVersionCode(manifest.hardware_version);
    if (expectedHardwareCode === null ||
        manifest.hardware_version_code !== expectedHardwareCode) {
        throw new Error('package hardware version metadata is invalid');
    }
    if (manifest.ota_scope !== 'STM32_ONLY' ||
        manifest.ch585_update !== 'MANUAL_INDEPENDENT_FLASH') {
        throw new Error('package must be STM32-only; CH585 is independently flashed');
    }
    if (manifest.hardware_version !== '2.0.0' ||
        expectedHardwareCode !== HARDWARE_VERSION_CODE_V2) {
        throw new Error('package hardware version must be 2.0.0');
    }
    if (manifest.signature_algorithm !==
            FIRMWARE_SIGNATURE_ECDSA_P256_SHA256 ||
        typeof manifest.firmware_hash !== 'string' ||
        !/^[0-9a-f]{64}$/.test(manifest.firmware_hash) ||
        typeof manifest.signature !== 'string' ||
        !/^[0-9a-f]{128}$/.test(manifest.signature) ||
        typeof manifest.trust_bundle_sha256 !== 'string' ||
        !/^[0-9a-f]{64}$/.test(manifest.trust_bundle_sha256) ||
        !Number.isInteger(manifest.security_version) ||
        manifest.security_version < MINIMUM_FIRMWARE_SECURITY_VERSION ||
        !Number.isInteger(manifest.build_timestamp) ||
        manifest.build_timestamp < 0 ||
        manifest.build_timestamp > 0xffffffff ||
        typeof manifest.build_date !== 'string' ||
        Buffer.byteLength(manifest.build_date, 'utf8') > 31 ||
        manifest.build_date.length === 0 ||
        typeof manifest.webresources_optional !== 'boolean') {
        throw new Error('manifest signed firmware metadata is invalid');
    }
    const metadataInfo = manifest.metadata;
    if (!metadataInfo || typeof metadataInfo !== 'object' ||
        Array.isArray(metadataInfo) ||
        metadataInfo.file !== SIGNED_METADATA_FILENAME ||
        metadataInfo.size !== METADATA_STRUCT_SIZE ||
        typeof metadataInfo.sha256 !== 'string' ||
        !/^[0-9a-f]{64}$/.test(metadataInfo.sha256)) {
        throw new Error('manifest metadata.bin descriptor is invalid');
    }
    if (!Array.isArray(manifest.components) ||
        manifest.components.length !== STM32_OTA_COMPONENTS.length) {
        throw new Error('manifest must contain exactly three STM32 components');
    }

    const componentNames = manifest.components.map(component => component && component.name);
    if (new Set(componentNames).size !== STM32_OTA_COMPONENTS.length ||
        STM32_OTA_COMPONENTS.some(name => !componentNames.includes(name))) {
        throw new Error('manifest component set is invalid');
    }

    const expectedEntries = new Set([
        'manifest.json',
        SIGNED_METADATA_FILENAME
    ]);
    const componentFiles = new Set();
    for (const component of manifest.components) {
        const layout = STM32_OTA_LAYOUT[slot][component.name];
        const optionalWebresources = component.name === 'webresources' &&
            manifest.webresources_optional &&
            component.active === false &&
            component.size === 0;
        if (optionalWebresources) {
            if (component.file !== '' || component.file_type !== 'none' ||
                component.sha256 !== '0'.repeat(64) ||
                parseManifestAddress(
                    component.address, `${component.name}.address`
                ) !== layout.address) {
                throw new Error('optional webresources metadata is invalid');
            }
            continue;
        }
        if (typeof component.file !== 'string' || !component.file ||
            component.file.includes('/') || component.file.includes('\\') ||
            Buffer.byteLength(component.file, 'utf8') > 63) {
            throw new Error(`invalid component filename: ${component.name}`);
        }
        if (component.file_type !== 'bin' || component.active === false) {
            throw new Error(`${component.name}.file_type must be bin`);
        }
        if (componentFiles.has(component.file)) {
            throw new Error('each component must use a different filename');
        }
        componentFiles.add(component.file);
        if (parseManifestAddress(component.address, `${component.name}.address`) !== layout.address) {
            throw new Error(`${component.name} address does not match slot ${slot}`);
        }
        if (!Number.isInteger(component.size) || component.size <= 0 ||
            component.size > layout.maxSize) {
            throw new Error(`${component.name} size is invalid`);
        }
        if (typeof component.sha256 !== 'string' ||
            !/^[0-9a-f]{64}$/.test(component.sha256)) {
            throw new Error(`${component.name} SHA-256 is invalid`);
        }
        const data = entries.get(component.file);
        if (!data || data.length !== component.size) {
            throw new Error(`${component.name} ZIP content size mismatch`);
        }
        const actualHash = crypto.createHash('sha256').update(data).digest('hex');
        if (actualHash !== component.sha256) {
            throw new Error(`${component.name} ZIP content SHA-256 mismatch`);
        }
        expectedEntries.add(component.file);
    }
    const metadata = entries.get(SIGNED_METADATA_FILENAME);
    if (!metadata || metadata.length !== metadataInfo.size) {
        throw new Error('metadata.bin ZIP content size mismatch');
    }
    const metadataHash = crypto.createHash('sha256')
        .update(metadata)
        .digest('hex');
    if (metadataHash !== metadataInfo.sha256) {
        throw new Error('metadata.bin ZIP content SHA-256 mismatch');
    }
    validateSignedMetadata(metadata, manifest, releasePublicKey);
    if (entries.size !== expectedEntries.size ||
        [...entries.keys()].some(name => !expectedEntries.has(name))) {
        throw new Error(
            'ZIP must contain only manifest.json, metadata.bin and active components'
        );
    }
    return manifest;
}

// 工具函数
function generateDownloadUrl(filename, serverUrl) {
    return `${serverUrl}/downloads/${filename}`;
}

function calculateFileHash(filePath) {
    try {
        const data = fs.readFileSync(filePath);
        return crypto.createHash('sha256').update(data).digest('hex');
    } catch (error) {
        console.error('计算文件哈希失败:', error.message);
        return null;
    }
}

// 版本号比较和验证函数
function compareVersions(version1, version2) {
    const v1Parts = version1.split('.').map(Number);
    const v2Parts = version2.split('.').map(Number);
    
    while (v1Parts.length < 3) v1Parts.push(0);
    while (v2Parts.length < 3) v2Parts.push(0);
    
    for (let i = 0; i < 3; i++) {
        if (v1Parts[i] < v2Parts[i]) return -1;
        if (v1Parts[i] > v2Parts[i]) return 1;
    }
    
    return 0;
}

function isValidVersion(version) {
    const versionPattern = /^\d+\.\d+\.\d+$/;
    return versionPattern.test(version);
}

function isValidHardwareVersion(hardwareVersion) {
    return hardwareVersionCode(hardwareVersion) !== null;
}

function presentFirmwareSlotForRequest(slot, req, config) {
    if (!slot) {
        return null;
    }
    if (req.deviceSession) {
        const filename = typeof slot.filename === 'string'
            ? slot.filename
            : slot.filePath;
        const safeFilename = typeof filename === 'string' &&
            filename.length > 0 &&
            !/[\\/]/.test(filename) &&
            filename !== '.' &&
            filename !== '..'
            ? filename
            : null;
        return {
            ...slot,
            /*
             * V2 bearer tokens are pinned to the authentication origin. Never
             * replay an absolute URL persisted when the package was uploaded;
             * expose only this server's protected relative download route.
             */
            downloadUrl: safeFilename
                ? `/downloads/${encodeURIComponent(safeFilename)}`
                : null
        };
    }
    if (!req.authenticatedDevice ||
        !req.app.locals.legacyDownloadTickets) {
        return null;
    }
    const downloadUrl = createLegacyDownloadUrl(
        req.app.locals.legacyDownloadTickets,
        config.serverUrl,
        slot,
        req.authenticatedDevice
    );
    if (!downloadUrl) {
        return null;
    }
    return {
        ...slot,
        downloadUrl
    };
}

function presentFirmwareForRequest(firmware, req, config) {
    if (!req.deviceSession) {
        return firmware;
    }
    return {
        ...firmware,
        slotA: presentFirmwareSlotForRequest(firmware.slotA, req, config),
        slotB: presentFirmwareSlotForRequest(firmware.slotB, req, config)
    };
}

function cleanupUploadedFiles(files) {
    if (!files || typeof files !== 'object') {
        return;
    }
    for (const group of Object.values(files)) {
        if (!Array.isArray(group)) {
            continue;
        }
        for (const file of group) {
            try {
                if (file && file.path && fs.existsSync(file.path)) {
                    fs.unlinkSync(file.path);
                }
            } catch (error) {
                console.error('Failed to clean rejected upload:', error.message);
            }
        }
    }
}

function findNewerFirmwares(currentVersion, hardwareVersion, firmwares) {
    if (!isValidVersion(currentVersion) || !isValidHardwareVersion(hardwareVersion)) {
        return [];
    }
    
    return firmwares
        .filter(firmware => {
            return firmware.hardwareVersion === hardwareVersion &&
                   isValidVersion(firmware.version) &&
                   compareVersions(firmware.version, currentVersion) > 0;
        })
        .sort((a, b) => compareVersions(b.version, a.version));
}

/**
 * 初始化所有路由
 */
function initAllRoutes(app, storage_manager, config, validateDeviceAuth, requireAdminAuth, authManager) {
    
    // ==================== 系统接口 ====================
    
    // 健康检查
    app.get('/health', (req, res) => {
        res.json({
            status: 'ok',
            message: 'STM32 HBox 固件服务器运行正常',
            timestamp: new Date().toISOString(),
            version: '1.0.0',
            deviceAuthV2Ready: Boolean(
                req.app.locals.deviceAuthV2 &&
                req.app.locals.deviceAuthV2.isReady()
            )
        });
    });

    // ==================== 设备管理接口 ====================
    
    // 设备注册接口
    app.post('/api/device/register', requireAdminAuth(), async (req, res) => {
        try {
            const { rawUniqueId, deviceId, deviceName } = req.body;
            
            console.log('📥 设备注册请求:');
            console.log('  设备名称 (deviceName):', deviceName);
            console.log('  管理员认证:', req.authenticatedAdmin ? '✅已认证' : '❌未认证');
            
            // 验证必需参数
            if (!rawUniqueId || !deviceId) {
                return res.status(400).json({
                    success: false,
                    message: '原始唯一ID和设备ID是必需的',
                    errNo: 1,
                    errorMessage: 'rawUniqueId and deviceId are required'
                });
            }

            // 验证原始唯一ID格式
            const uniqueIdPattern = /^[A-Fa-f0-9]{8}-[A-Fa-f0-9]{8}-[A-Fa-f0-9]{8}$/;
            if (!uniqueIdPattern.test(rawUniqueId.trim())) {
                return res.status(400).json({
                    success: false,
                    message: '原始唯一ID格式错误，必须是 XXXXXXXX-XXXXXXXX-XXXXXXXX 格式',
                    errNo: 1,
                    errorMessage: 'rawUniqueId format error, must be XXXXXXXX-XXXXXXXX-XXXXXXXX format'
                });
            }

            // 验证设备ID格式
            const deviceIdPattern = /^[A-Fa-f0-9]{16}$/;
            if (!deviceIdPattern.test(deviceId.trim())) {
                return res.status(400).json({
                    success: false,
                    message: '设备ID格式错误，必须是16位十六进制字符串',
                    errNo: 1,
                    errorMessage: 'deviceId format error, must be 16-digit hexadecimal string'
                });
            }

            // 构建设备信息
            const deviceInfo = {
                rawUniqueId: rawUniqueId.trim(),
                deviceId: deviceId.trim().toUpperCase(),
                deviceName: deviceName ? deviceName.trim() : `HBox-${deviceId.trim().substring(0, 8)}`,
                registerIP: req.ip || req.connection.remoteAddress || 'unknown',
                registeredBy: req.authenticatedAdmin.username
            };

            // 使用异步方式注册设备
            const result = await storage_manager.addDeviceAsync(deviceInfo);
            
            if (result.success) {
                const statusCode = result.existed ? 200 : 201;
                res.status(statusCode).json({
                    success: true,
                    message: result.message,
                    errNo: 0,
                    data: {
                        deviceId: result.device.deviceId,
                        deviceName: result.device.deviceName,
                        registerTime: result.device.registerTime,
                        registeredBy: result.device.registeredBy,
                        existed: result.existed
                    }
                });
                
                if (!result.existed) {
                    console.log(`Device registered successfully by ${req.authenticatedAdmin.username}: ${result.device.deviceName} (${result.device.deviceId})`);
                } else {
                    console.log(`Device already exists: ${result.device.deviceName} (${result.device.deviceId})`);
                }
            } else {
                res.status(400).json({
                    success: false,
                    message: result.message,
                    errNo: 1,
                    errorMessage: result.message
                });
            }

        } catch (error) {
            console.error('Device registration failed:', error);
            res.status(500).json({
                success: false,
                message: '设备注册失败',
                errNo: 1,
                errorMessage: 'Device registration failed: ' + error.message,
                error: error.message
            });
        }
    });

    // 获取设备列表接口
    app.get('/api/devices', validateDeviceAuth(), (req, res) => {
        try {
            const devices = storage_manager.getDevices();
            res.json({
                success: true,
                data: devices,
                total: devices.length,
                timestamp: new Date().toISOString()
            });
        } catch (error) {
            console.error('获取设备列表失败:', error);
            res.status(500).json({
                success: false,
                message: '获取设备列表失败',
                error: error.message
            });
        }
    });

    // ==================== 管理员认证接口 ====================

    // 管理员登录验证接口
    app.post('/api/admin/login', requireAdminAuth({ source: 'body' }), (req, res) => {
        try {
            res.json({
                success: true,
                message: '登录成功',
                data: {
                    username: req.authenticatedAdmin.username,
                    role: req.authenticatedAdmin.role,
                    loginTime: new Date().toISOString()
                }
            });
            
            console.log(`管理员登录成功: ${req.authenticatedAdmin.username}`);
        } catch (error) {
            console.error('管理员登录处理失败:', error);
            res.status(500).json({
                success: false,
                message: '登录处理失败',
                error: error.message
            });
        }
    });

    // 修改管理员密码接口
    app.post('/api/admin/change-password', requireAdminAuth({ source: 'body' }), (req, res) => {
        try {
            const { currentPassword, newPassword } = req.body;
            
            if (!currentPassword || !newPassword) {
                return res.status(400).json({
                    success: false,
                    message: '当前密码和新密码都是必需的',
                    errNo: 1,
                    errorMessage: 'Current password and new password are required'
                });
            }
            
            if (newPassword.length < 6) {
                return res.status(400).json({
                    success: false,
                    message: '新密码长度不能少于6位',
                    errNo: 1,
                    errorMessage: 'New password must be at least 6 characters long'
                });
            }
            
            const result = authManager.changeAdminPassword(currentPassword, newPassword);
            
            if (result.success) {
                res.json({
                    success: true,
                    message: result.message,
                    data: {
                        changedBy: req.authenticatedAdmin.username,
                        changeTime: new Date().toISOString()
                    }
                });
                
                console.log(`管理员密码修改成功: ${req.authenticatedAdmin.username}`);
            } else {
                res.status(400).json({
                    success: false,
                    message: result.message,
                    errNo: 1,
                    errorMessage: result.message
                });
            }
            
        } catch (error) {
            console.error('修改管理员密码失败:', error);
            res.status(500).json({
                success: false,
                message: '修改密码失败',
                errNo: 1,
                errorMessage: 'Password change failed: ' + error.message,
                error: error.message
            });
        }
    });

    // 获取账户信息接口
    app.get('/api/admin/profile', requireAdminAuth(), (req, res) => {
        try {
            res.json({
                success: true,
                data: {
                    username: req.authenticatedAdmin.username,
                    role: req.authenticatedAdmin.role,
                    lastUpdate: authManager.config.admin.lastUpdate,
                    requestTime: new Date().toISOString()
                }
            });
        } catch (error) {
            console.error('获取管理员信息失败:', error);
            res.status(500).json({
                success: false,
                message: '获取账户信息失败',
                error: error.message
            });
        }
    });

    // ==================== 固件管理接口 ====================

    // 配置文件上传
    const storage = multer.diskStorage({
        destination: (req, file, cb) => {
            cb(null, config.uploadDir);
        },
        filename: (req, file, cb) => {
            const timestamp = Date.now();
            const ext = path.extname(file.originalname);
            const name = path.basename(file.originalname, ext);
            cb(null, `${timestamp}_${name}${ext}`);
        }
    });

    const upload = multer({
        storage: storage,
        limits: {
            fileSize: config.maxFileSize
        },
        fileFilter: (req, file, cb) => {
            const ext = path.extname(file.originalname).toLowerCase();
            if (config.allowedExtensions.includes(ext)) {
                cb(null, true);
            } else {
                cb(new Error(`只允许上传 ${config.allowedExtensions.join(', ')} 格式的文件`));
            }
        }
    });

    const validateLegacyFirmwareCatalog =
        validateDeviceAuth({ source: 'headers' });
    const validateV2FirmwareCatalog = app.locals.deviceAuthV2
        ? app.locals.deviceAuthV2.requireSession(['config.read'])
        : null;
    const validateFirmwareCatalog = (req, res, next) => {
        if (validateV2FirmwareCatalog &&
            /^Bearer /.test(req.get('authorization') || '')) {
            return validateV2FirmwareCatalog(req, res, next);
        }
        return validateLegacyFirmwareCatalog(req, res, next);
    };

    // 1. 获取固件列表
    app.get('/api/firmwares', validateFirmwareCatalog, (req, res) => {
        try {
            const firmwares = storage_manager.getFirmwares();
            
            // 只返回基本信息，过滤敏感信息
            const filteredFirmwares = firmwares.map(firmware => ({
                name: firmware.name,
                version: firmware.version,
                hardwareVersion: firmware.hardwareVersion,
                desc: firmware.desc
            }));
            
            res.json({
                success: true,
                data: filteredFirmwares,
                total: filteredFirmwares.length,
                timestamp: new Date().toISOString()
            });
        } catch (error) {
            console.error('获取固件列表失败:', error);
            res.status(500).json({
                success: false,
                message: '获取固件列表失败',
                error: error.message
            });
        }
    });

    // 2. 检查固件更新
    const validateLegacyFirmwareCheck = validateDeviceAuth({ source: 'body' });
    /*
     * Reading the signed firmware catalog is non-destructive. The elevated
     * firmware.update scope is required later by the protected package
     * download and device-side upgrade commands.
     */
    const validateV2FirmwareCheck = validateV2FirmwareCatalog;
    const validateFirmwareCheck = (req, res, next) => {
        if (validateV2FirmwareCheck &&
            /^Bearer /.test(req.get('authorization') || '')) {
            return validateV2FirmwareCheck(req, res, next);
        }
        return validateLegacyFirmwareCheck(req, res, next);
    };
    app.post('/api/firmware-check-update', validateFirmwareCheck, (req, res) => {
        try {
            const {
                currentVersion,
                hardwareVersion: submittedHardwareVersion
            } = req.body;
            const hardwareVersion = req.deviceSession
                ? req.deviceSession.hardwareVersion
                : submittedHardwareVersion;
            
            if (!currentVersion) {
                return res.status(400).json({
                    success: false,
                    message: 'current version is required',
                    errNo: 1,
                    errorMessage: 'current version is required'
                });
            }

            if (!isValidVersion(currentVersion.trim())) {
                return res.status(400).json({
                    success: false,
                    message: 'version format error, must be three-digit version format (e.g. 1.0.0)',
                    errNo: 1,
                    errorMessage: 'version format error, must be three-digit version format (e.g. 1.0.0)'
                });
            }

            /*
             * Old clients did not send a hardware version. They must not be
             * offered V2 by version number alone; fail closed instead.
             */
            if (!isValidHardwareVersion(hardwareVersion)) {
                return res.status(400).json({
                    success: false,
                    message: 'hardware version is required',
                    errNo: 1,
                    errorMessage: 'hardwareVersion is required and must use x.y.z format'
                });
            }
            if (req.deviceSession &&
                submittedHardwareVersion !== undefined &&
                submittedHardwareVersion !== hardwareVersion) {
                return res.status(403).json({
                    success: false,
                    message: 'hardware version does not match authenticated device',
                    errNo: 1,
                    errorMessage: 'hardwareVersion does not match device session'
                });
            }

            const allFirmwares = storage_manager.getFirmwares();
            const normalizedHardwareVersion = hardwareVersion.trim();
            const newerFirmwares = findNewerFirmwares(
                currentVersion.trim(),
                normalizedHardwareVersion,
                allFirmwares
            );
            const updateAvailable = newerFirmwares.length > 0;
            const latestFirmware = updateAvailable ? newerFirmwares[0] : null;
            
            const responseData = {
                currentVersion: currentVersion.trim(),
                hardwareVersion: normalizedHardwareVersion,
                updateAvailable: updateAvailable,
                updateCount: newerFirmwares.length,
                checkTime: new Date().toISOString()
            };

            if (updateAvailable) {
                responseData.latestVersion = latestFirmware.version;
                responseData.latestFirmware = {
                    id: latestFirmware.id,
                    name: latestFirmware.name,
                    version: latestFirmware.version,
                    hardwareVersion: latestFirmware.hardwareVersion,
                    desc: latestFirmware.desc,
                    createTime: latestFirmware.createTime,
                    updateTime: latestFirmware.updateTime,
                    slotA: presentFirmwareSlotForRequest(
                        latestFirmware.slotA,
                        req,
                        config
                    ),
                    slotB: presentFirmwareSlotForRequest(
                        latestFirmware.slotB,
                        req,
                        config
                    )
                };
                responseData.availableUpdates = newerFirmwares.map(firmware => ({
                    id: firmware.id,
                    name: firmware.name,
                    version: firmware.version,
                    hardwareVersion: firmware.hardwareVersion,
                    desc: firmware.desc,
                    createTime: firmware.createTime
                }));
            }

            res.json({
                success: true,
                errNo: 0,
                data: responseData,
                message: updateAvailable ? 
                    `found ${newerFirmwares.length} updates, latest version: ${latestFirmware.version}` : 
                    'current version is the latest'
            });

            console.log(`Firmware update check: hardware ${normalizedHardwareVersion}, current version ${currentVersion.trim()}, ${updateAvailable ? `found ${newerFirmwares.length} updates` : 'no updates'}`);

        } catch (error) {
            console.error('Firmware update check failed:', error);
            res.status(500).json({
                success: false,
                message: 'Firmware update check failed',
                errNo: 1,
                errorMessage: 'Firmware update check failed: ' + error.message,
                error: error.message
            });
        }
    });

    // 3. 固件包上传
    app.post('/api/firmwares/upload', requireAdminAuth(), upload.fields([
        { name: 'slotA', maxCount: 1 },
        { name: 'slotB', maxCount: 1 }
    ]), async (req, res) => {
        const rejectUpload = (status, message) => {
            cleanupUploadedFiles(req.files);
            return res.status(status).json({ success: false, message });
        };
        try {
            const { version, desc, hardwareVersion: submittedHardwareVersion } = req.body;
            
            if (!version) {
                return rejectUpload(400, '版本号是必需的');
            }

            if (!isValidVersion(version.trim())) {
                return rejectUpload(
                    400,
                    'version format error, must be three-digit version format (e.g. 1.0.0)'
                );
            }

            if (!req.files || (!req.files.slotA && !req.files.slotB)) {
                return rejectUpload(400, 'at least one slot of firmware package is required');
            }

            const packageManifests = {};
            if (req.files.slotA && req.files.slotA[0]) {
                packageManifests.slotA = validateUploadedOtaPackage(
                    req.files.slotA[0].path,
                    'A',
                    config.firmwareReleasePublicKey
                );
            }
            if (req.files.slotB && req.files.slotB[0]) {
                packageManifests.slotB = validateUploadedOtaPackage(
                    req.files.slotB[0].path,
                    'B',
                    config.firmwareReleasePublicKey
                );
            }
            const manifests = Object.values(packageManifests);
            const packageVersions = new Set(manifests.map(manifest => manifest.version));
            const hardwareVersions = new Set(
                manifests.map(manifest => manifest.hardware_version)
            );
            if (packageVersions.size !== 1 || hardwareVersions.size !== 1) {
                return rejectUpload(
                    400,
                    'slot packages must have identical version and hardware_version'
                );
            }

            const packageVersion = manifests[0].version;
            const hardwareVersion = manifests[0].hardware_version;
            if (version.trim() !== packageVersion) {
                return rejectUpload(400, 'form version does not match package manifest');
            }
            if (submittedHardwareVersion !== undefined &&
                submittedHardwareVersion.trim() !== hardwareVersion) {
                return rejectUpload(
                    400,
                    'form hardwareVersion does not match package manifest'
                );
            }

            const existingFirmware = storage_manager.getFirmwares().find(f =>
                f.version === packageVersion &&
                f.hardwareVersion === hardwareVersion
            );
            if (existingFirmware) {
                return rejectUpload(
                    409,
                    `hardware ${hardwareVersion} version ${packageVersion} already exists`
                );
            }

            const firmware = {
                name: `HBox ${hardwareVersion} firmware ${packageVersion}`,
                version: packageVersion,
                hardwareVersion,
                desc: desc ? desc.trim() : '',
                slotA: null,
                slotB: null
            };

            if (req.files.slotA && req.files.slotA[0]) {
                const file = req.files.slotA[0];
                firmware.slotA = {
                    originalName: file.originalname,
                    filename: file.filename,
                    filePath: file.filename,
                    fileSize: file.size,
                    downloadUrl: generateDownloadUrl(file.filename, config.serverUrl),
                    uploadTime: new Date().toISOString(),
                    hash: calculateFileHash(file.path),
                    manifest: packageManifests.slotA
                };
            }

            if (req.files.slotB && req.files.slotB[0]) {
                const file = req.files.slotB[0];
                firmware.slotB = {
                    originalName: file.originalname,
                    filename: file.filename,
                    filePath: file.filename,
                    fileSize: file.size,
                    downloadUrl: generateDownloadUrl(file.filename, config.serverUrl),
                    uploadTime: new Date().toISOString(),
                    hash: calculateFileHash(file.path),
                    manifest: packageManifests.slotB
                };
            }

            if (storage_manager.addFirmware(firmware)) {
                res.json({
                    success: true,
                    message: 'firmware uploaded successfully',
                    data: firmware
                });
                console.log(`Firmware uploaded successfully: ${firmware.name} v${firmware.version}`);
            } else {
                return rejectUpload(
                    409,
                    'failed to save firmware information or duplicate hardware/version key'
                );
            }

        } catch (error) {
            cleanupUploadedFiles(req.files);
            console.error('Firmware upload failed:', error);
            res.status(400).json({
                success: false,
                message: 'Firmware upload failed',
                error: error.message
            });
        }
    });

    // 4. 固件包删除
    app.delete('/api/firmwares/:id', requireAdminAuth(), (req, res) => {
        try {
            const { id } = req.params;
            
            const firmware = storage_manager.findFirmware(id);
            if (!firmware) {
                return res.status(404).json({
                    success: false,
                    message: 'firmware not found'
                });
            }

            if (storage_manager.deleteFirmware(id)) {
                res.json({
                    success: true,
                    message: 'firmware deleted successfully',
                    data: { id, name: firmware.name, version: firmware.version }
                });
                console.log(`Firmware deleted successfully: ${firmware.name} v${firmware.version}`);
            } else {
                res.status(500).json({
                    success: false,
                    message: 'failed to delete firmware'
                });
            }

        } catch (error) {
            console.error('Firmware deletion failed:', error);
            res.status(500).json({
                success: false,
                message: 'Firmware deletion failed',
                error: error.message
            });
        }
    });

    // 5. 清空指定版本及之前的所有版本固件
    app.post('/api/firmwares/clear-up-to-version', requireAdminAuth(), (req, res) => {
        try {
            const { targetVersion, hardwareVersion } = req.body;
            
            if (!targetVersion) {
                return res.status(400).json({
                    success: false,
                    message: 'target version is required'
                });
            }

            if (!isValidVersion(targetVersion.trim())) {
                return res.status(400).json({
                    success: false,
                    message: 'version format error, must be three-digit version format (e.g. 1.0.0)'
                });
            }

            if (!isValidHardwareVersion(hardwareVersion)) {
                return res.status(400).json({
                    success: false,
                    message: 'hardwareVersion is required and must use x.y.z format'
                });
            }

            const normalizedHardwareVersion = hardwareVersion.trim();
            const result = storage_manager.clearFirmwaresUpToVersion(
                targetVersion.trim(),
                normalizedHardwareVersion
            );
            
            if (result.success) {
                res.json({
                    success: true,
                    message: `successfully cleared ${result.deletedCount} firmware(s) for hardware ${normalizedHardwareVersion} up to version ${targetVersion.trim()}`,
                    data: {
                        targetVersion: targetVersion.trim(),
                        hardwareVersion: normalizedHardwareVersion,
                        deletedCount: result.deletedCount,
                        deletedFirmwares: result.deletedFirmwares,
                        clearTime: new Date().toISOString()
                    }
                });
                console.log(`Firmware clearing completed: hardware ${normalizedHardwareVersion}, cleared ${result.deletedCount} firmware(s) up to version ${targetVersion.trim()}`);
            } else {
                res.status(500).json({
                    success: false,
                    message: 'failed to clear firmware data'
                });
            }

        } catch (error) {
            console.error('Firmware clearing failed:', error);
            res.status(500).json({
                success: false,
                message: 'Firmware clearing failed',
                error: error.message
            });
        }
    });

    // 6. 获取单个固件详情
    app.get('/api/firmwares/:id', validateFirmwareCatalog, (req, res) => {
        try {
            const { id } = req.params;
            const firmware = storage_manager.findFirmware(id);
            
            if (!firmware) {
                return res.status(404).json({
                    success: false,
                    message: 'firmware not found'
                });
            }

            res.json({
                success: true,
                data: presentFirmwareForRequest(firmware, req, config)
            });

        } catch (error) {
            console.error('Failed to get firmware details:', error);
            res.status(500).json({
                success: false,
                message: 'Failed to get firmware details',
                error: error.message
            });
        }
    });

    // 7. 更新固件信息
    app.put('/api/firmwares/:id', requireAdminAuth(), (req, res) => {
        try {
            const { id } = req.params;
            const { name, version, desc } = req.body;
            
            const firmware = storage_manager.findFirmware(id);
            if (!firmware) {
                return res.status(404).json({
                    success: false,
                    message: 'firmware not found'
                });
            }

            const updates = {};
            if (name !== undefined) updates.name = name.trim();
            if (version !== undefined) {
                if (!isValidVersion(version.trim())) {
                    return res.status(400).json({
                        success: false,
                        message: 'version format error, must be three-digit version format (e.g. 1.0.0)'
                    });
                }
                const duplicate = storage_manager.getFirmwares().find(candidate =>
                    candidate.id !== id &&
                    candidate.hardwareVersion === firmware.hardwareVersion &&
                    candidate.version === version.trim()
                );
                if (duplicate) {
                    return res.status(409).json({
                        success: false,
                        message: `hardware ${firmware.hardwareVersion} version ${version.trim()} already exists`
                    });
                }
                updates.version = version.trim();
            }
            if (desc !== undefined) updates.desc = desc.trim();

            if (storage_manager.updateFirmware(id, updates)) {
                const updatedFirmware = storage_manager.findFirmware(id);
                res.json({
                    success: true,
                    message: 'firmware information updated successfully',
                    data: updatedFirmware
                });
                console.log(`Firmware information updated successfully: ${updatedFirmware.name} v${updatedFirmware.version}`);
            } else {
                res.status(500).json({
                    success: false,
                    message: 'failed to update firmware information'
                });
            }

        } catch (error) {
            console.error('Firmware information update failed:', error);
            res.status(500).json({
                success: false,
                message: 'Firmware information update failed',
                error: error.message
            });
        }
    });
}

module.exports = {
    initAllRoutes,
    findNewerFirmwares,
    readFlatZipEntries,
    validateUploadedOtaPackage
};
