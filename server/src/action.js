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

// 工具函数
function generateDownloadUrl(filename, serverUrl) {
    return `${serverUrl}/downloads/${filename}`;
}

function calculateFileHash(filePath) {
    const fs = require('fs-extra');
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

function findNewerFirmwares(currentVersion, firmwares) {
    if (!isValidVersion(currentVersion)) {
        return [];
    }
    
    return firmwares
        .filter(firmware => {
            return isValidVersion(firmware.version) && 
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
            version: '1.0.0'
        });
    });

    // ==================== 设备管理接口 ====================
    
    // 设备注册接口
    app.post('/api/device/register', requireAdminAuth(), async (req, res) => {
        try {
            const { rawUniqueId, deviceId, deviceName } = req.body;
            
            console.log('📥 设备注册请求:');
            console.log('  原始唯一ID (rawUniqueId):', rawUniqueId);
            console.log('  设备ID (deviceId):', deviceId);
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

    // 1. 获取固件列表
    app.get('/api/firmwares', (req, res) => {
        try {
            const firmwares = storage_manager.getFirmwares();
            
            // 只返回基本信息，过滤敏感信息
            const filteredFirmwares = firmwares.map(firmware => ({
                name: firmware.name,
                version: firmware.version,
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
    app.post('/api/firmware-check-update', validateDeviceAuth({ source: 'body' }), (req, res) => {
        try {
            const { currentVersion } = req.body;
            
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

            const allFirmwares = storage_manager.getFirmwares();
            const newerFirmwares = findNewerFirmwares(currentVersion.trim(), allFirmwares);
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
    app.post('/api/firmwares/upload', requireAdminAuth(), upload.fields([
        { name: 'slotA', maxCount: 1 },
        { name: 'slotB', maxCount: 1 }
    ]), async (req, res) => {
        try {
            const { version, desc } = req.body;
            
            if (!version) {
                return res.status(400).json({
                    success: false,
                    message: '版本号是必需的'
                });
            }

            const versionPattern = /^\d+\.\d+\.\d+$/;
            if (!versionPattern.test(version.trim())) {
                return res.status(400).json({
                    success: false,
                    message: 'version format error, must be three-digit version format (e.g. 1.0.0)'
                });
            }

            const existingFirmware = storage_manager.getFirmwares().find(f => f.version === version.trim());
            if (existingFirmware) {
                return res.status(409).json({
                    success: false,
                    message: `version ${version.trim()} already exists, not allowed to upload again`
                });
            }

            if (!req.files || (!req.files.slotA && !req.files.slotB)) {
                return res.status(400).json({
                    success: false,
                    message: 'at least one slot of firmware package is required'
                });
            }

            const firmware = {
                name: `HBox firmware ${version.trim()}`,
                version: version.trim(),
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
                    hash: calculateFileHash(file.path)
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
                    hash: calculateFileHash(file.path)
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
            const { targetVersion } = req.body;
            
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

    // 6. 获取单个固件详情
    app.get('/api/firmwares/:id', (req, res) => {
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

    // 7. 更新固件信息
    app.put('/api/firmwares/:id', (req, res) => {
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
            if (version !== undefined) updates.version = version.trim();
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
    initAllRoutes
}; 