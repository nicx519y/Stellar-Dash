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

// 2. 固件包上传
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
                message: '版本号格式错误，必须是三位版本号格式（如：1.0.0）'
            });
        }

        // 检查版本号是否已存在
        const existingFirmware = storage_manager.getFirmwares().find(f => f.version === version.trim());
        if (existingFirmware) {
            return res.status(409).json({
                success: false,
                message: `版本号 ${version.trim()} 已存在，不允许重复上传`
            });
        }

        // 检查是否至少上传了一个槽的文件
        if (!req.files || (!req.files.slotA && !req.files.slotB)) {
            return res.status(400).json({
                success: false,
                message: '至少需要上传一个槽的固件包'
            });
        }

        // 构建固件对象
        const firmware = {
            name: `HBox固件 ${version.trim()}`, // 自动生成名称
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
                message: '固件上传成功',
                data: firmware
            });
            console.log(`固件上传成功: ${firmware.name} v${firmware.version}`);
        } else {
            res.status(500).json({
                success: false,
                message: '保存固件信息失败'
            });
        }

    } catch (error) {
        console.error('固件上传失败:', error);
        res.status(500).json({
            success: false,
            message: '固件上传失败',
            error: error.message
        });
    }
});

// 3. 固件包删除
app.delete('/api/firmwares/:id', (req, res) => {
    try {
        const { id } = req.params;
        
        // 查找固件
        const firmware = storage_manager.findFirmware(id);
        if (!firmware) {
            return res.status(404).json({
                success: false,
                message: '固件不存在'
            });
        }

        // 删除固件
        if (storage_manager.deleteFirmware(id)) {
            res.json({
                success: true,
                message: '固件删除成功',
                data: { id, name: firmware.name, version: firmware.version }
            });
            console.log(`固件删除成功: ${firmware.name} v${firmware.version}`);
        } else {
            res.status(500).json({
                success: false,
                message: '删除固件失败'
            });
        }

    } catch (error) {
        console.error('固件删除失败:', error);
        res.status(500).json({
            success: false,
            message: '固件删除失败',
            error: error.message
        });
    }
});

// 4. 获取单个固件详情
app.get('/api/firmwares/:id', (req, res) => {
    try {
        const { id } = req.params;
        const firmware = storage_manager.findFirmware(id);
        
        if (!firmware) {
            return res.status(404).json({
                success: false,
                message: '固件不存在'
            });
        }

        res.json({
            success: true,
            data: firmware
        });

    } catch (error) {
        console.error('获取固件详情失败:', error);
        res.status(500).json({
            success: false,
            message: '获取固件详情失败',
            error: error.message
        });
    }
});

// 5. 更新固件信息
app.put('/api/firmwares/:id', (req, res) => {
    try {
        const { id } = req.params;
        const { name, version, desc } = req.body;
        
        // 查找固件
        const firmware = storage_manager.findFirmware(id);
        if (!firmware) {
            return res.status(404).json({
                success: false,
                message: '固件不存在'
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
                message: '固件信息更新成功',
                data: updatedFirmware
            });
            console.log(`固件信息更新成功: ${updatedFirmware.name} v${updatedFirmware.version}`);
        } else {
            res.status(500).json({
                success: false,
                message: '更新固件信息失败'
            });
        }

    } catch (error) {
        console.error('固件信息更新失败:', error);
        res.status(500).json({
            success: false,
            message: '固件信息更新失败',
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
                message: `文件大小不能超过 ${config.maxFileSize / (1024 * 1024)}MB`
            });
        }
    }
    
    console.error('服务器错误:', error);
    res.status(500).json({
        success: false,
        message: '服务器内部错误',
        error: error.message
    });
});

// 404 处理
app.use((req, res) => {
    res.status(404).json({
        success: false,
        message: '接口不存在',
        path: req.path
    });
});

// 优雅关闭处理
process.on('SIGINT', () => {
    console.log('\n收到中断信号，正在关闭服务器...');
    process.exit(0);
});

process.on('SIGTERM', () => {
    console.log('\n收到终止信号，正在关闭服务器...');
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
    console.log('  POST   /api/firmwares/upload   - 上传固件包');
    console.log('  GET    /api/firmwares/:id      - 获取固件详情');
    console.log('  PUT    /api/firmwares/:id      - 更新固件信息');
    console.log('  DELETE /api/firmwares/:id      - 删除固件包');
    console.log('  GET    /downloads/:filename    - 下载固件包');
    console.log('='.repeat(60));
    console.log('服务器启动成功！按 Ctrl+C 停止服务');
});

module.exports = app; 