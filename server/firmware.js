const express = require('express');
const multer = require('multer');
const path = require('path');
const fs = require('fs-extra');
const crypto = require('crypto');

// 创建路由器
const router = express.Router();

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

function findNewerFirmwares(currentVersion, firmwares) {
    /**
     * 查找比当前版本更新的固件
     * @param {string} currentVersion - 当前设备版本号
     * @param {Array} firmwares - 固件列表
     * @returns {Array} - 更新的固件列表，按版本号降序排列
     */
    if (!isValidVersion(currentVersion)) {
        return [];
    }
    
    return firmwares
        .filter(firmware => {
            return isValidVersion(firmware.version) && 
                   compareVersions(firmware.version, currentVersion) > 0;
        })
        .sort((a, b) => compareVersions(b.version, a.version)); // 降序排列，最新版本在前
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

// 初始化固件路由
function initFirmwareRoutes(storage_manager, config, validateDeviceAuth) {
    // 1. 获取固件列表
    router.get('/api/firmwares', (req, res) => {
        try {
            const firmwares = storage_manager.getFirmwares();
            res.json({
                success: true,
                data: firmwares,
                total: firmwares.length,
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
    router.post('/api/firmware-check-update', validateDeviceAuth({ source: 'body' }), (req, res) => {
        try {
            const { currentVersion } = req.body;
            
            // 验证当前版本号参数
            if (!currentVersion) {
                return res.status(400).json({
                    success: false,
                    message: 'current version is required',
                    errNo: 1,
                    errorMessage: 'current version is required'
                });
            }

            // 验证版本号格式
            if (!isValidVersion(currentVersion.trim())) {
                return res.status(400).json({
                    success: false,
                    message: 'version format error, must be three-digit version format (e.g. 1.0.0)',
                    errNo: 1,
                    errorMessage: 'version format error, must be three-digit version format (e.g. 1.0.0)'
                });
            }

            // 获取所有固件
            const allFirmwares = storage_manager.getFirmwares();
            
            // 查找更新的固件
            const newerFirmwares = findNewerFirmwares(currentVersion.trim(), allFirmwares);
            
            // 构建响应数据
            const updateAvailable = newerFirmwares.length > 0;
            const latestFirmware = updateAvailable ? newerFirmwares[0] : null;
            
            const responseData = {
                currentVersion: currentVersion.trim(),
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
                    desc: latestFirmware.desc,
                    createTime: latestFirmware.createTime,
                    updateTime: latestFirmware.updateTime,
                    slotA: latestFirmware.slotA,
                    slotB: latestFirmware.slotB
                };
                responseData.availableUpdates = newerFirmwares.map(firmware => ({
                    id: firmware.id,
                    name: firmware.name,
                    version: firmware.version,
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

            console.log(`Firmware update check: current version ${currentVersion.trim()}, ${updateAvailable ? `found ${newerFirmwares.length} updates` : 'no updates'}`);

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
    const storage = multer.diskStorage({
        destination: (req, file, cb) => {
            cb(null, config.uploadDir);
        },
        filename: (req, file, cb) => {
            // 生成唯一文件名: timestamp_原始名称
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

    router.post('/api/firmwares/upload', upload.fields([
        { name: 'slotA', maxCount: 1 },
        { name: 'slotB', maxCount: 1 }
    ]), async (req, res) => {
        try {
            const { version, desc } = req.body;
            
            // 验证版本号
            if (!version) {
                return res.status(400).json({
                    success: false,
                    message: '版本号是必需的'
                });
            }

            // 验证版本号格式：必须是x.y.z格式
            const versionPattern = /^\d+\.\d+\.\d+$/;
            if (!versionPattern.test(version.trim())) {
                return res.status(400).json({
                    success: false,
                    message: 'version format error, must be three-digit version format (e.g. 1.0.0)'
                });
            }

            // 检查版本号是否已存在
            const existingFirmware = storage_manager.getFirmwares().find(f => f.version === version.trim());
            if (existingFirmware) {
                return res.status(409).json({
                    success: false,
                    message: `version ${version.trim()} already exists, not allowed to upload again`
                });
            }

            // 检查是否至少上传了一个槽的文件
            if (!req.files || (!req.files.slotA && !req.files.slotB)) {
                return res.status(400).json({
                    success: false,
                    message: 'at least one slot of firmware package is required'
                });
            }

            // 构建固件对象
            const firmware = {
                name: `HBox firmware ${version.trim()}`, // 自动生成名称
                version: version.trim(),
                desc: desc ? desc.trim() : '',
                slotA: null,
                slotB: null
            };

            // 处理槽A文件
            if (req.files.slotA && req.files.slotA[0]) {
                const file = req.files.slotA[0];
                firmware.slotA = {
                    originalName: file.originalname,
                    filename: file.filename,
                    filePath: file.filename,
                    fileSize: file.size,
                    downloadUrl: generateDownloadUrl(file.filename, config.serverUrl),
                    uploadTime: new Date().toISOString(),
                    hash: calculateFileHash(file.path)
                };
            }

            // 处理槽B文件
            if (req.files.slotB && req.files.slotB[0]) {
                const file = req.files.slotB[0];
                firmware.slotB = {
                    originalName: file.originalname,
                    filename: file.filename,
                    filePath: file.filename,
                    fileSize: file.size,
                    downloadUrl: generateDownloadUrl(file.filename, config.serverUrl),
                    uploadTime: new Date().toISOString(),
                    hash: calculateFileHash(file.path)
                };
            }

            // 保存到存储
            if (storage_manager.addFirmware(firmware)) {
                res.json({
                    success: true,
                    message: 'firmware uploaded successfully',
                    data: firmware
                });
                console.log(`Firmware uploaded successfully: ${firmware.name} v${firmware.version}`);
            } else {
                res.status(500).json({
                    success: false,
                    message: 'failed to save firmware information'
                });
            }

        } catch (error) {
            console.error('Firmware upload failed:', error);
            res.status(500).json({
                success: false,
                message: 'Firmware upload failed',
                error: error.message
            });
        }
    });

    // 4. 固件包删除
    router.delete('/api/firmwares/:id', (req, res) => {
        try {
            const { id } = req.params;
            
            // 查找固件
            const firmware = storage_manager.findFirmware(id);
            if (!firmware) {
                return res.status(404).json({
                    success: false,
                    message: 'firmware not found'
                });
            }

            // 删除固件
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

    // 4.1. 清空指定版本及之前的所有版本固件
    router.post('/api/firmwares/clear-up-to-version', (req, res) => {
        try {
            const { targetVersion } = req.body;
            
            // 验证目标版本号参数
            if (!targetVersion) {
                return res.status(400).json({
                    success: false,
                    message: 'target version is required'
                });
            }

            // 验证版本号格式
            if (!isValidVersion(targetVersion.trim())) {
                return res.status(400).json({
                    success: false,
                    message: 'version format error, must be three-digit version format (e.g. 1.0.0)'
                });
            }

            // 执行清理操作
            const result = storage_manager.clearFirmwaresUpToVersion(targetVersion.trim());
            
            if (result.success) {
                res.json({
                    success: true,
                    message: `successfully cleared ${result.deletedCount} firmware(s) up to version ${targetVersion.trim()}`,
                    data: {
                        targetVersion: targetVersion.trim(),
                        deletedCount: result.deletedCount,
                        deletedFirmwares: result.deletedFirmwares,
                        clearTime: new Date().toISOString()
                    }
                });
                console.log(`Firmware clearing completed: cleared ${result.deletedCount} firmware(s) up to version ${targetVersion.trim()}`);
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

    // 5. 获取单个固件详情
    router.get('/api/firmwares/:id', (req, res) => {
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
                data: firmware
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

    // 6. 更新固件信息
    router.put('/api/firmwares/:id', (req, res) => {
        try {
            const { id } = req.params;
            const { name, version, desc } = req.body;
            
            // 查找固件
            const firmware = storage_manager.findFirmware(id);
            if (!firmware) {
                return res.status(404).json({
                    success: false,
                    message: 'firmware not found'
                });
            }

            // 准备更新数据
            const updates = {};
            if (name !== undefined) updates.name = name.trim();
            if (version !== undefined) updates.version = version.trim();
            if (desc !== undefined) updates.desc = desc.trim();

            // 更新固件信息
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

    return router;
}

module.exports = { initFirmwareRoutes }; 