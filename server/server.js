#!/usr/bin/env node
// -*- coding: utf-8 -*-

/**
 * STM32 HBox 固件管理服务器
 * 
 * 功能:
 * 1. 固件版本列表管理
 * 2. 固件包上传和存储
 * 3. 固件包下载
 * 4. 固件包删除
 */

const express = require('express');
const multer = require('multer');
const cors = require('cors');
const path = require('path');
const fs = require('fs-extra');
const crypto = require('crypto');

const app = express();
const PORT = process.env.PORT || 3000;

// 配置目录
const config = {
    uploadDir: path.join(__dirname, 'uploads'),
    dataFile: path.join(__dirname, 'firmware_list.json'),
    maxFileSize: 50 * 1024 * 1024, // 50MB
    allowedExtensions: ['.zip'],
    serverUrl: process.env.SERVER_URL || `http://localhost:${PORT}`
};

// 中间件配置
app.use(cors());
app.use(express.json());
app.use(express.urlencoded({ extended: true }));

// 静态文件服务 - 用于下载固件包
app.use('/downloads', express.static(config.uploadDir));

// 创建上传目录
fs.ensureDirSync(config.uploadDir);

// 配置文件上传
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

// 数据存储管理
class FirmwareStorage {
    constructor() {
        this.dataFile = config.dataFile;
        this.data = this.loadData();
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

    // 获取所有固件
    getFirmwares() {
        return this.data.firmwares;
    }

    // 添加固件
    addFirmware(firmware) {
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
            this.data.firmwares[index] = {
                ...this.data.firmwares[index],
                ...updates,
                updateTime: new Date().toISOString()
            };
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
    clearFirmwaresUpToVersion(targetVersion) {
        const toDelete = [];
        const toKeep = [];
        
        this.data.firmwares.forEach(firmware => {
            if (isValidVersion(firmware.version) && isValidVersion(targetVersion)) {
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
                version: f.version
            }))
        };
    }

    // 删除固件相关文件
    deleteFiles(firmware) {
        try {
            if (firmware.slotA && firmware.slotA.filePath) {
                const fullPath = path.join(config.uploadDir, path.basename(firmware.slotA.filePath));
                if (fs.existsSync(fullPath)) {
                    fs.unlinkSync(fullPath);
                    console.log(`已删除槽A文件: ${fullPath}`);
                }
            }
            if (firmware.slotB && firmware.slotB.filePath) {
                const fullPath = path.join(config.uploadDir, path.basename(firmware.slotB.filePath));
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

// 创建存储实例
const storage_manager = new FirmwareStorage();

// 工具函数
function generateDownloadUrl(filename) {
    return `${config.serverUrl}/downloads/${filename}`;
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

// ==================== API 路由 ====================

// 健康检查
app.get('/health', (req, res) => {
    res.json({
        status: 'ok',
        message: 'STM32 HBox 固件服务器运行正常',
        timestamp: new Date().toISOString(),
        version: '1.0.0'
    });
});

// 1. 获取固件列表
app.get('/api/firmwares', (req, res) => {
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
app.post('/api/firmware-check-update', (req, res) => {
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
app.post('/api/firmwares/upload', upload.fields([
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
                downloadUrl: generateDownloadUrl(file.filename),
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
                downloadUrl: generateDownloadUrl(file.filename),
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
app.delete('/api/firmwares/:id', (req, res) => {
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
app.post('/api/firmwares/clear-up-to-version', (req, res) => {
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

// 6. 更新固件信息
app.put('/api/firmwares/:id', (req, res) => {
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

// 错误处理中间件
app.use((error, req, res, next) => {
    if (error instanceof multer.MulterError) {
        if (error.code === 'LIMIT_FILE_SIZE') {
            return res.status(400).json({
                success: false,
                message: `file size cannot exceed ${config.maxFileSize / (1024 * 1024)}MB`
            });
        }
    }
    
    console.error('服务器错误:', error);
    res.status(500).json({
        success: false,
        message: 'Server internal error',
        error: error.message
    });
});

// 404 处理
app.use((req, res) => {
    res.status(404).json({
        success: false,
        message: 'API not found',
        path: req.path
    });
});

// 优雅关闭处理
process.on('SIGINT', () => {
    console.log('\nReceived interrupt signal, shutting down server...');
    process.exit(0);
});

process.on('SIGTERM', () => {
    console.log('\nReceived termination signal, shutting down server...');
    process.exit(0);
});

// 启动服务器
app.listen(PORT, () => {
    console.log('='.repeat(60));
    console.log('STM32 HBox 固件管理服务器');
    console.log('='.repeat(60));
    console.log(`服务器地址: http://localhost:${PORT}`);
    console.log(`上传目录: ${config.uploadDir}`);
    console.log(`数据文件: ${config.dataFile}`);
    console.log(`最大文件大小: ${config.maxFileSize / (1024 * 1024)}MB`);
    console.log(`支持文件类型: ${config.allowedExtensions.join(', ')}`);
    console.log('='.repeat(60));
    console.log('可用接口:');
    console.log('  GET    /health                 - 健康检查');
    console.log('  GET    /api/firmwares          - 获取固件列表');
    console.log('  POST   /api/firmware-check-update - 检查固件更新');
    console.log('  POST   /api/firmwares/upload   - 上传固件包');
    console.log('  GET    /api/firmwares/:id      - 获取固件详情');
    console.log('  PUT    /api/firmwares/:id      - 更新固件信息');
    console.log('  DELETE /api/firmwares/:id      - 删除固件包');
    console.log('  POST   /api/firmwares/clear-up-to-version - 清空指定版本及之前的固件');
    console.log('  GET    /downloads/:filename    - 下载固件包');
    console.log('='.repeat(60));
    console.log('服务器启动成功！按 Ctrl+C 停止服务');
});

module.exports = app; 