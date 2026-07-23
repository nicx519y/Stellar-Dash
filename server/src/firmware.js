const express = require('express');
const multer = require('multer');
const path = require('path');
const fs = require('fs-extra');
const crypto = require('crypto');

const LATEST_HARDWARE_VERSION = '2.0.0';
const STM32_OTA_COMPONENTS = Object.freeze([
    'application',
    'webresources',
    'adc_mapping'
]);

function isValidHardwareVersion(hardwareVersion) {
    if (typeof hardwareVersion !== 'string') {
        return false;
    }
    const match = /^(\d+)\.(\d+)\.(\d+)$/.exec(hardwareVersion);
    return Boolean(match) && match.slice(1).every(part => Number(part) <= 255);
}

function applyStm32OtaMetadata(firmware) {
    return {
        ...firmware,
        otaComponents: [...STM32_OTA_COMPONENTS],
        otaScope: 'STM32_ONLY',
        ch585Included: false,
        ch585UpdatePolicy: 'MANUAL_INDEPENDENT_FLASH'
    };
}

// 数据存储管理
class FirmwareStorage {
    constructor(dataFile, uploadDir) {
        this.dataFile = dataFile;
        this.deviceDataFile = path.join(path.dirname(dataFile), 'device_ids.json');
        this.uploadDir = uploadDir;
        this.data = this.loadData();
        this.deviceData = this.loadDeviceData();
    }

    // 加载数据
    loadData() {
        try {
            if (fs.existsSync(this.dataFile)) {
                const content = fs.readFileSync(this.dataFile, 'utf8');
                return JSON.parse(content);
            }
        } catch (error) {
            console.error('加载数据失败:', error.message);
        }
        
        // 返回默认数据结构
        return {
            firmwares: [],
            lastUpdate: new Date().toISOString()
        };
    }

    // 加载设备数据
    loadDeviceData() {
        try {
            if (fs.existsSync(this.deviceDataFile)) {
                const content = fs.readFileSync(this.deviceDataFile, 'utf8');
                return JSON.parse(content);
            }
        } catch (error) {
            console.error('加载设备数据失败:', error.message);
        }
        
        // 返回默认设备数据结构
        return {
            devices: [],
            lastUpdate: new Date().toISOString()
        };
    }

    // 保存数据
    saveData() {
        try {
            this.data.lastUpdate = new Date().toISOString();
            fs.writeFileSync(this.dataFile, JSON.stringify(this.data, null, 2), 'utf8');
            return true;
        } catch (error) {
            console.error('保存数据失败:', error.message);
            return false;
        }
    }

    // 保存设备数据
    saveDeviceData() {
        try {
            this.deviceData.lastUpdate = new Date().toISOString();
            fs.writeFileSync(this.deviceDataFile, JSON.stringify(this.deviceData, null, 2), 'utf8');
            return true;
        } catch (error) {
            console.error('保存设备数据失败:', error.message);
            return false;
        }
    }

    // 计算设备ID哈希 (与固件中的算法保持一致)
    calculateDeviceIdHash(uid_word0, uid_word1, uid_word2) {
        // 与 utils.c 中相同的哈希算法
        // 盐值常量
        const salt1 = 0x48426F78;  // "HBox"
        const salt2 = 0x32303234;  // "2024"
        
        // 质数常量
        const prime1 = 0x9E3779B9;  // 黄金比例的32位表示
        const prime2 = 0x85EBCA6B;  // 另一个质数
        const prime3 = 0xC2B2AE35;  // 第三个质数
        
        // 第一轮哈希
        let hash1 = uid_word0 ^ salt1;
        hash1 = ((hash1 << 13) | (hash1 >>> 19)) >>> 0;  // 左循环移位13位
        hash1 = Math.imul(hash1, prime1) >>> 0;  // 使用Math.imul进行32位乘法
        hash1 ^= uid_word1;
        
        // 第二轮哈希
        let hash2 = uid_word1 ^ salt2;
        hash2 = ((hash2 << 17) | (hash2 >>> 15)) >>> 0;  // 左循环移位17位
        hash2 = Math.imul(hash2, prime2) >>> 0;  // 使用Math.imul进行32位乘法
        hash2 ^= uid_word2;
        
        // 第三轮哈希
        let hash3 = uid_word2 ^ ((salt1 + salt2) >>> 0);
        hash3 = ((hash3 << 21) | (hash3 >>> 11)) >>> 0;  // 左循环移位21位
        hash3 = Math.imul(hash3, prime3) >>> 0;  // 使用Math.imul进行32位乘法
        hash3 ^= hash1;
        
        // 最终组合
        const final_hash1 = (hash1 ^ hash2) >>> 0;
        const final_hash2 = (hash2 ^ hash3) >>> 0;
        
        // 转换为16位十六进制字符串 (64位哈希)
        const device_id = final_hash1.toString(16).toUpperCase().padStart(8, '0') + 
                         final_hash2.toString(16).toUpperCase().padStart(8, '0');
        
        return device_id;
    }

    // 验证设备ID哈希
    verifyDeviceIdHash(rawUniqueId, deviceIdHash) {
        try {
            // 解析原始唯一ID格式: XXXXXXXX-XXXXXXXX-XXXXXXXX
            const parts = rawUniqueId.split('-');
            if (parts.length !== 3) {
                return false;
            }
            
            const uid_word0 = parseInt(parts[0], 16);
            const uid_word1 = parseInt(parts[1], 16);
            const uid_word2 = parseInt(parts[2], 16);
            
            // 计算期望的哈希值
            const expectedHash = this.calculateDeviceIdHash(uid_word0, uid_word1, uid_word2);
            
            // 比较哈希值
            return expectedHash === deviceIdHash.toUpperCase();
        } catch (error) {
            console.error('验证设备ID哈希失败:', error.message);
            return false;
        }
    }

    // 查找设备
    findDevice(deviceId) {
        return this.deviceData.devices.find(d => d.deviceId === deviceId.toUpperCase());
    }
    
    // 异步保存设备数据
    async saveDeviceDataAsync() {
        try {
            const dataString = JSON.stringify(this.deviceData, null, 2);
            await fs.promises.writeFile(this.deviceDataFile, dataString, 'utf8');
            return true;
        } catch (error) {
            console.error('异步保存设备数据失败:', error.message);
            return false;
        }
    }
    
    // 异步添加设备
    async addDeviceAsync(deviceInfo) {
        // 查找已存在的设备
        const existingDevice = this.findDevice(deviceInfo.deviceId);
        if (existingDevice) {
            return {
                success: true,
                existed: true,
                message: '设备已存在',
                device: existingDevice
            };
        }
        
        // 验证设备ID哈希
        if (!this.verifyDeviceIdHash(deviceInfo.rawUniqueId, deviceInfo.deviceId)) {
            return {
                success: false,
                message: '设备ID哈希验证失败'
            };
        }
        
        // 添加新设备
        const newDevice = {
            ...deviceInfo,
            registerTime: new Date().toISOString(),
            lastSeen: new Date().toISOString(),
            status: 'active'
        };
        
        this.deviceData.devices.push(newDevice);
        
        if (await this.saveDeviceDataAsync()) {
            return {
                success: true,
                existed: false,
                message: '设备注册成功',
                device: newDevice
            };
        } else {
            return {
                success: false,
                message: '保存设备数据失败'
            };
        }
    }

    // 添加设备
    addDevice(deviceInfo) {
        // 查找已存在的设备
        const existingDevice = this.findDevice(deviceInfo.deviceId);
        if (existingDevice) {
            return {
                success: true,
                existed: true,
                message: '设备已存在',
                device: existingDevice
            };
        }
        
        // 🔍 调试打印：哈希验证过程
        console.log('🔧 设备ID哈希验证:');
        console.log('  输入原始唯一ID:', deviceInfo.rawUniqueId);
        console.log('  输入设备ID:', deviceInfo.deviceId);
        
        // 计算服务器端的设备ID哈希
        const parts = deviceInfo.rawUniqueId.split('-');
        const uid_word0 = parseInt(parts[0], 16);
        const uid_word1 = parseInt(parts[1], 16);
        const uid_word2 = parseInt(parts[2], 16);
        const serverCalculatedDeviceId = this.calculateDeviceIdHash(uid_word0, uid_word1, uid_word2);
        
        console.log('  服务器计算的设备ID:', serverCalculatedDeviceId);
        console.log('  验证结果:', serverCalculatedDeviceId === deviceInfo.deviceId.toUpperCase() ? '✅ 匹配' : '❌ 不匹配');
        
        // 验证设备ID哈希
        if (!this.verifyDeviceIdHash(deviceInfo.rawUniqueId, deviceInfo.deviceId)) {
            return {
                success: false,
                message: '设备ID哈希验证失败'
            };
        }
        
        // 添加新设备
        const newDevice = {
            ...deviceInfo,
            registerTime: new Date().toISOString(),
            lastSeen: new Date().toISOString(),
            status: 'active'
        };
        
        this.deviceData.devices.push(newDevice);
        
        if (this.saveDeviceData()) {
            return {
                success: true,
                existed: false,
                message: '设备注册成功',
                device: newDevice
            };
        } else {
            return {
                success: false,
                message: '保存设备数据失败'
            };
        }
    }

    // 获取所有设备
    getDevices() {
        return this.deviceData.devices;
    }

    // 获取所有固件
    getFirmwares() {
        return this.data.firmwares;
    }

    // 添加固件
    addFirmware(firmware) {
        if (!isValidVersion(firmware.version) ||
            !isValidHardwareVersion(firmware.hardwareVersion)) {
            return false;
        }
        const duplicate = this.data.firmwares.some(existing =>
            existing.version === firmware.version &&
            existing.hardwareVersion === firmware.hardwareVersion
        );
        if (duplicate) {
            return false;
        }
        Object.assign(firmware, applyStm32OtaMetadata(firmware));
        // 生成唯一ID
        firmware.id = this.generateId();
        firmware.createTime = new Date().toISOString();
        firmware.updateTime = new Date().toISOString();
        
        this.data.firmwares.push(firmware);
        return this.saveData();
    }

    // 更新固件
    updateFirmware(id, updates) {
        const index = this.data.firmwares.findIndex(f => f.id === id);
        if (index !== -1) {
            /*
             * Do not silently reclassify a historical V1 release as V2 when
             * an operator only edits its notes or rollout state. New uploads
             * receive the V2 metadata in addFirmware(); an explicit update
             * may still change these fields when that is actually intended.
             */
            const candidate = {
                ...this.data.firmwares[index],
                ...updates
            };
            if (!isValidVersion(candidate.version) ||
                !isValidHardwareVersion(candidate.hardwareVersion)) {
                return false;
            }
            const duplicate = this.data.firmwares.some((existing, candidateIndex) =>
                candidateIndex !== index &&
                existing.version === candidate.version &&
                existing.hardwareVersion === candidate.hardwareVersion
            );
            if (duplicate) {
                return false;
            }
            candidate.updateTime = new Date().toISOString();
            this.data.firmwares[index] = candidate;
            return this.saveData();
        }
        return false;
    }

    // 删除固件
    deleteFirmware(id) {
        const index = this.data.firmwares.findIndex(f => f.id === id);
        if (index !== -1) {
            const firmware = this.data.firmwares[index];
            this.data.firmwares.splice(index, 1);
            
            // 删除相关文件
            this.deleteFiles(firmware);
            
            return this.saveData();
        }
        return false;
    }

    // 根据ID查找固件
    findFirmware(id) {
        return this.data.firmwares.find(f => f.id === id);
    }

    // 清空指定版本及之前的所有版本固件
    clearFirmwaresUpToVersion(targetVersion, hardwareVersion) {
        const toDelete = [];
        const toKeep = [];
        
        this.data.firmwares.forEach(firmware => {
            if (firmware.hardwareVersion !== hardwareVersion) {
                toKeep.push(firmware);
            } else if (isValidVersion(firmware.version) && isValidVersion(targetVersion)) {
                if (compareVersions(firmware.version, targetVersion) <= 0) {
                    toDelete.push(firmware);
                } else {
                    toKeep.push(firmware);
                }
            } else {
                // 如果版本号格式不正确，保留固件
                toKeep.push(firmware);
            }
        });
        
        // 删除文件
        toDelete.forEach(firmware => {
            this.deleteFiles(firmware);
        });
        
        // 更新固件列表
        this.data.firmwares = toKeep;
        
        return {
            success: this.saveData(),
            deletedCount: toDelete.length,
            deletedFirmwares: toDelete.map(f => ({
                id: f.id,
                name: f.name,
                version: f.version,
                hardwareVersion: f.hardwareVersion
            }))
        };
    }

    // 删除固件相关文件
    deleteFiles(firmware) {
        try {
            if (firmware.slotA && firmware.slotA.filePath) {
                const fullPath = path.join(this.uploadDir, path.basename(firmware.slotA.filePath));
                if (fs.existsSync(fullPath)) {
                    fs.unlinkSync(fullPath);
                    console.log(`已删除槽A文件: ${fullPath}`);
                }
            }
            if (firmware.slotB && firmware.slotB.filePath) {
                const fullPath = path.join(this.uploadDir, path.basename(firmware.slotB.filePath));
                if (fs.existsSync(fullPath)) {
                    fs.unlinkSync(fullPath);
                    console.log(`已删除槽B文件: ${fullPath}`);
                }
            }
        } catch (error) {
            console.error('删除文件失败:', error.message);
        }
    }

    // 生成唯一ID
    generateId() {
        return crypto.randomBytes(16).toString('hex');
    }
}

// 版本号比较工具函数
function compareVersions(version1, version2) {
    /**
     * 比较两个版本号
     * @param {string} version1 - 第一个版本号 (如 "1.0.0")
     * @param {string} version2 - 第二个版本号 (如 "1.0.1")
     * @returns {number} - 返回 -1 (version1 < version2), 0 (相等), 1 (version1 > version2)
     */
    const v1Parts = version1.split('.').map(Number);
    const v2Parts = version2.split('.').map(Number);
    
    // 确保版本号都是三位数
    while (v1Parts.length < 3) v1Parts.push(0);
    while (v2Parts.length < 3) v2Parts.push(0);
    
    for (let i = 0; i < 3; i++) {
        if (v1Parts[i] < v2Parts[i]) return -1;
        if (v1Parts[i] > v2Parts[i]) return 1;
    }
    
    return 0;
}

function isValidVersion(version) {
    /**
     * 验证版本号格式是否正确
     * @param {string} version - 版本号字符串
     * @returns {boolean} - 是否为有效的三位版本号格式
     */
    const versionPattern = /^\d+\.\d+\.\d+$/;
    return versionPattern.test(version);
}

module.exports = { 
    FirmwareStorage,
    compareVersions,
    isValidVersion,
    isValidHardwareVersion,
    LATEST_HARDWARE_VERSION
};
