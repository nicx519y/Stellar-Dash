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
const cors = require('cors');
const path = require('path');
const fs = require('fs-extra');

// 引入固件模块
const { FirmwareStorage, compareVersions, isValidVersion } = require('./firmware');

// 引入账户认证模块
const { authManager, requireAdminAuth } = require('./auth');

// 引入设备认证模块
const { validateDeviceAuth } = require('./device-auth');

// 引入网络接口入口模块
const { initAllRoutes } = require('./action');

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

// 创建存储实例
const storage_manager = new FirmwareStorage(config.dataFile, config.uploadDir);

// 将storage_manager添加到app.locals以供中间件使用
app.locals.storage_manager = storage_manager;

// ==================== 初始化所有路由 ====================
initAllRoutes(app, storage_manager, config, validateDeviceAuth, requireAdminAuth, authManager);

// 错误处理中间件
app.use((error, req, res, next) => {
    const multer = require('multer');
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
    console.log('  POST   /api/device/register    - 注册设备ID (需要管理员认证)');
    console.log('  GET    /api/devices            - 获取设备列表 (需要设备认证)');
    console.log('  POST   /api/admin/login        - 管理员登录验证');
    console.log('  POST   /api/admin/change-password - 修改管理员密码 (需要管理员认证)');
    console.log('  GET    /api/admin/profile      - 获取管理员信息 (需要管理员认证)');
    console.log('  GET    /api/firmwares          - 获取固件列表');
    console.log('  POST   /api/firmware-check-update - 检查固件更新 (需要设备认证)');
    console.log('  POST   /api/firmwares/upload   - 上传固件包 (需要管理员认证)');
    console.log('  GET    /api/firmwares/:id      - 获取固件详情');
    console.log('  PUT    /api/firmwares/:id      - 更新固件信息');
    console.log('  DELETE /api/firmwares/:id      - 删除固件包 (需要管理员认证)');
    console.log('  POST   /api/firmwares/clear-up-to-version - 清空指定版本及之前的固件 (需要管理员认证)');
    console.log('  GET    /downloads/:filename    - 下载固件包');
    console.log('='.repeat(60));
    console.log('服务器启动成功！按 Ctrl+C 停止服务');
});

module.exports = app; 