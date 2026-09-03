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
const crypto = require('crypto');
const fs = require('fs-extra');
const server_address = process.env.SERVER_ADDRESS || '182.92.72.220';
const server_port = process.env.SERVER_PORT || 3000;
const domain_name = process.env.DOMAIN_NAME || 'firmware.st-dash.com';

// 引入固件模块
const { FirmwareStorage, compareVersions, isValidVersion } = require('./firmware');

// 引入设备认证模块
const { validateDeviceAuth } = require('./device-auth');
const {
    createDeviceAuthV2FromEnvironment,
    initDeviceAuthV2Routes
} = require('./device-auth-v2');
const {
    parseAllowedOrigins,
    parseTrustedProxyHops,
    createExactCorsOptions,
    securityHeaders
} = require('./http-security');
const {
    LegacyDownloadTicketStore,
    initLegacyDownloadRoute
} = require('./download-access');
const {
    installHostedWebConfig,
    resolveHostedWebConfigOptions
} = require('./hosted-webconfig');
const { resolveServerStoragePaths } = require('./server-paths');
const { DeviceAccountStore } = require('./device-account-store');
const { UserAccountStore } = require('./user-account-store');
const {
    createEmailAuthFromEnvironment,
    initEmailAuthRoutes
} = require('./email-auth');
const {
    createAdminAccessFromEnvironment,
    initAdminRoutes
} = require('./admin-access');
const {
    SwitchMappingStore,
    initSwitchMappingRoutes
} = require('./switch-mappings');
const {
    ImageGalleryStore,
    LocalGalleryStorage,
    initImageGalleryRoutes
} = require('./image-gallery');

// 引入网络接口入口模块
const { initAllRoutes } = require('./action');

const app = express();
const PORT = process.env.PORT || 3000;
const LISTEN_HOST = process.env.LISTEN_HOST ||
    (process.env.NODE_ENV === 'production' ? '127.0.0.1' : '0.0.0.0');
const trustedProxyHops = parseTrustedProxyHops(
    process.env.TRUST_PROXY_HOPS
);
const storagePaths = resolveServerStoragePaths();

function loadFirmwareReleasePublicKey(environment = process.env) {
    const inline = environment.FIRMWARE_RELEASE_PUBLIC_KEY_PEM;
    const fileName = String(
        environment.FIRMWARE_RELEASE_PUBLIC_KEY_FILE || ''
    ).trim();
    if (environment.NODE_ENV === 'production') {
        if (inline) {
            throw new Error(
                'FIRMWARE_RELEASE_PUBLIC_KEY_PEM is not accepted in production'
            );
        }
        if (!fileName) {
            throw new Error(
                'FIRMWARE_RELEASE_PUBLIC_KEY_FILE is required in production'
            );
        }
        if (!path.isAbsolute(fileName)) {
            throw new Error(
                'FIRMWARE_RELEASE_PUBLIC_KEY_FILE must be absolute in production'
            );
        }
    }
    const material = inline
        ? inline.replace(/\\n/g, '\n')
        : fileName
            ? fs.readFileSync(fileName, 'utf8')
            : null;
    if (!material) {
        return null;
    }
    const publicKey = crypto.createPublicKey(material);
    if (publicKey.asymmetricKeyType !== 'ec' ||
        publicKey.asymmetricKeyDetails?.namedCurve !== 'prime256v1') {
        throw new Error('firmware release public key must be P-256');
    }
    return publicKey;
}

// 配置目录
const config = {
    uploadDir: storagePaths.uploadDir,
    dataFile: storagePaths.firmwareDataFile,
    firmwareReleasePublicKey: loadFirmwareReleasePublicKey(),
    maxFileSize: 50 * 1024 * 1024, // 50MB
    allowedExtensions: ['.zip'],
    // 支持多种访问方式
    serverUrls: {
        direct: process.env.DIRECT_URL || `http://${server_address}:${server_port}`,
        domain: process.env.DOMAIN_URL || `https://${domain_name}`
    },
    // 兼容旧配置
    serverUrl: process.env.SERVER_URL || `http://${server_address}:${server_port}`
};

const allowedWebConfigOrigins = parseAllowedOrigins(
    process.env.WEB_CONFIG_ORIGINS,
    `https://${domain_name}`
);

// 中间件配置
app.disable('x-powered-by');
if (trustedProxyHops > 0) {
    /*
     * Use a hop count only when the Node listener is behind the controlled
     * reverse proxy. Never trust arbitrary X-Forwarded-For by default.
     */
    app.set('trust proxy', trustedProxyHops);
}
app.use(securityHeaders);
app.use(cors(createExactCorsOptions(allowedWebConfigOrigins)));
app.use(express.json({ limit: '64kb', strict: true }));
app.use(express.urlencoded({ extended: true }));

// 创建持久状态与上传目录
fs.ensureDirSync(storagePaths.dataDir);
fs.ensureDirSync(config.uploadDir);
fs.ensureDirSync(storagePaths.galleryAssetDir);

// 创建存储实例
const storage_manager = new FirmwareStorage(config.dataFile, config.uploadDir);

// 将storage_manager添加到app.locals以供中间件使用
app.locals.storage_manager = storage_manager;

const deviceAccountStore = new DeviceAccountStore({
    databasePath: storagePaths.accountDatabase
});
app.locals.deviceAccountStore = deviceAccountStore;

const userAccountStore = new UserAccountStore({
    databasePath: storagePaths.userAccountDatabase
});
app.locals.userAccountStore = userAccountStore;
const emailAuth = createEmailAuthFromEnvironment({
    store: userAccountStore,
    allowedOrigins: allowedWebConfigOrigins
});
app.locals.emailAuth = emailAuth;
const adminAccess = createAdminAccessFromEnvironment({
    store: userAccountStore,
    emailAuth
});
const requireAdminAuth = options => adminAccess.requireAdmin(options);
app.locals.adminAccess = adminAccess;
userAccountStore.cleanupExpired();
const userAccountCleanupTimer = setInterval(
    () => userAccountStore.cleanupExpired(),
    60 * 60 * 1000
);
userAccountCleanupTimer.unref();

const deviceAuthV2 = createDeviceAuthV2FromEnvironment(storage_manager, {
    accountStore: deviceAccountStore
});
app.locals.deviceAuthV2 = deviceAuthV2;
const legacyDownloadTickets = new LegacyDownloadTicketStore();
app.locals.legacyDownloadTickets = legacyDownloadTickets;

const switchMappingStore = new SwitchMappingStore({
    databasePath: storagePaths.switchMappingDatabase
});
app.locals.switchMappingStore = switchMappingStore;

const imageGalleryStore = new ImageGalleryStore({
    databasePath: storagePaths.imageGalleryDatabase,
    userLimit: Number(process.env.HBOX_USER_GALLERY_LIMIT) || 10
});
const imageGalleryStorage = new LocalGalleryStorage({
    root: storagePaths.galleryAssetDir
});
app.locals.imageGalleryStore = imageGalleryStore;
app.locals.imageGalleryStorage = imageGalleryStorage;

/*
 * Shipped V1 clients cannot attach an Authorization header to the package
 * fetch. Their already-weak authentication may mint only a short-lived,
 * random path ticket on this isolated compatibility route.
 */
initLegacyDownloadRoute(
    app,
    legacyDownloadTickets,
    storage_manager,
    config.uploadDir
);

/*
 * Firmware files are no longer public. Browsers fetch them with the short
 * lived opaque token issued by the V2 attestation flow. Authorization is
 * accepted only from the header, never from a query string which may leak
 * into proxies and access logs.
 */
app.use(
    '/downloads',
    deviceAuthV2.requireSession(['firmware.update']),
    (req, res, next) => {
        res.set('Cache-Control', 'private, no-store');
        next();
    },
    express.static(config.uploadDir, {
        fallthrough: false,
        dotfiles: 'deny',
        index: false
    })
);

// ==================== 初始化所有路由 ====================
initDeviceAuthV2Routes(
    app,
    deviceAuthV2,
    storage_manager,
    requireAdminAuth
);
initEmailAuthRoutes(app, emailAuth);
initAdminRoutes(app, adminAccess);
initSwitchMappingRoutes(app, {
    store: switchMappingStore,
    deviceAuth: deviceAuthV2,
    adminAccess
});
initImageGalleryRoutes(app, {
    store: imageGalleryStore,
    storage: imageGalleryStorage,
    emailAuth,
    deviceAuth: deviceAuthV2,
    adminAccess
});
initAllRoutes(app, storage_manager, config, validateDeviceAuth, requireAdminAuth);

/*
 * The V2 WebConfig is a normal HTTPS-hosted static export. API and download
 * routes are registered first so no exported file can shadow them.
 *
 * Production deployment sets WEB_CONFIG_STATIC_DIR to the copied Next.js
 * export and WEB_CONFIG_REQUIRE_STATIC=1. Development/API-only processes may
 * omit the directory without weakening any device authorization boundary.
 */
const hostedWebConfigOptions = resolveHostedWebConfigOptions();
hostedWebConfigOptions.profileSlugs =
    deviceAuthV2.webConfigTargetPolicy?.profileSlugs || [];
const hostedWebConfig = installHostedWebConfig(app, hostedWebConfigOptions);
app.locals.hostedWebConfig = hostedWebConfig;

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
    
    const status = Number.isInteger(error.status) ? error.status : 500;
    if (status >= 500) {
        console.error('服务器错误:', error);
    }
    res.status(status).json({
        success: false,
        error: error.code || 'SERVER_ERROR',
        message: status >= 500 ? 'Server internal error' : error.message
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
app.listen(PORT, LISTEN_HOST, () => {
    console.log('='.repeat(60));
    console.log('STM32 HBox 固件管理服务器');
    console.log('='.repeat(60));
    console.log(`服务器地址: http://${server_address}:${server_port}`);
    console.log(`Node监听地址: ${LISTEN_HOST}:${PORT}`);
    console.log(`域名地址: https://${domain_name}`);
    console.log(`上传目录: ${config.uploadDir}`);
    console.log(`图库目录: ${storagePaths.galleryAssetDir}`);
    console.log(`数据文件: ${config.dataFile}`);
    console.log(`最大文件大小: ${config.maxFileSize / (1024 * 1024)}MB`);
    console.log(`支持文件类型: ${config.allowedExtensions.join(', ')}`);
    console.log(
        hostedWebConfig.enabled
            ? `WebConfig静态目录: ${hostedWebConfig.staticDir}`
            : 'WebConfig静态目录: 未启用'
    );
    if (deviceAuthV2.localDeviceAuthBypass) {
        console.warn(
            '警告: 本地设备信任策略已跳过；仅允许回环地址调试，WebHID RPC 仍加密'
        );
    }
    console.log('='.repeat(60));
    console.log('可用接口:');
    console.log('  GET    /health                 - 健康检查');
    console.log('  POST   /api/device/register    - 注册设备ID (需要管理员认证)');
    console.log('  POST   /api/v2/device-auth/challenges - 创建设备认证挑战');
    console.log('  POST   /api/v2/device-auth/verify - 验证设备证明并签发会话');
    console.log('  GET    /api/auth/session         - 获取邮箱账号登录状态');
    console.log('  POST   /api/auth/register/email/request - 发送邮箱验证链接');
    console.log('  POST   /api/auth/login/email     - 邮箱密码登录');
    console.log('  POST   /api/auth/logout          - 退出邮箱账号');
    console.log('  GET    /api/devices            - 获取设备列表 (需要设备认证)');
    console.log('  GET    /api/admin/profile      - 获取邮箱管理员信息');
    console.log('  GET    /api/admin/users        - 管理邮箱账号角色');
    console.log('  GET    /api/admin/service-tokens - 管理自动化服务令牌');
    console.log('  GET    /api/gallery/system      - 获取官方图库');
    console.log('  GET    /api/gallery/match       - 按设备图片指纹匹配图库');
    console.log('  GET    /api/gallery/mine        - 获取个人图库');
    console.log('  GET    /api/admin/gallery/system - 管理官方图库');
    console.log('  GET    /api/firmwares          - 获取固件列表 (需要 config.read scope)');
    console.log('  POST   /api/firmware-check-update - 检查固件更新 (需要设备认证)');
    console.log('  POST   /api/firmwares/upload   - 上传固件包 (需要管理员认证)');
    console.log('  GET    /api/firmwares/:id      - 获取固件详情 (需要 config.read scope)');
    console.log('  PUT    /api/firmwares/:id      - 更新固件信息');
    console.log('  DELETE /api/firmwares/:id      - 删除固件包 (需要管理员认证)');
    console.log('  POST   /api/firmwares/clear-up-to-version - 清空指定版本及之前的固件 (需要管理员认证)');
    console.log('  GET    /downloads/:filename    - 下载固件包 (需要 firmware.update scope)');
    console.log('='.repeat(60));
    console.log('服务器启动成功！按 Ctrl+C 停止服务');
});

module.exports = app;
