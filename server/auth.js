#!/usr/bin/env node
// -*- coding: utf-8 -*-

/**
 * 账户认证模块
 * 
 * 功能:
 * 1. admin账户管理
 * 2. 密码验证
 * 3. 认证中间件
 */

const crypto = require('crypto');
const fs = require('fs-extra');
const path = require('path');

class AuthManager {
    constructor() {
        this.configFile = path.join(__dirname, 'auth_config.json');
        this.config = this.loadConfig();
    }

    // 加载认证配置
    loadConfig() {
        try {
            if (fs.existsSync(this.configFile)) {
                const content = fs.readFileSync(this.configFile, 'utf8');
                return JSON.parse(content);
            }
        } catch (error) {
            console.error('加载认证配置失败:', error.message);
        }
        
        // 返回默认配置
        const defaultConfig = {
            admin: {
                username: 'admin',
                // 默认密码：admin123，已哈希
                passwordHash: this.hashPassword('admin123'),
                salt: this.generateSalt(),
                lastUpdate: new Date().toISOString()
            },
            settings: {
                sessionTimeout: 24 * 60 * 60, // 24小时(秒)
                maxLoginAttempts: 5,
                lockoutDuration: 15 * 60 // 15分钟(秒)
            }
        };
        
        this.saveConfig(defaultConfig);
        console.log('✨ 创建默认认证配置');
        console.log('   默认管理员账户: admin');
        console.log('   默认密码: admin123');
        console.log('   🚨 建议首次启动后立即修改密码!');
        
        return defaultConfig;
    }

    // 保存认证配置
    saveConfig(config = null) {
        try {
            const configToSave = config || this.config;
            configToSave.lastUpdate = new Date().toISOString();
            fs.writeFileSync(this.configFile, JSON.stringify(configToSave, null, 2), 'utf8');
            return true;
        } catch (error) {
            console.error('保存认证配置失败:', error.message);
            return false;
        }
    }

    // 生成盐值
    generateSalt() {
        return crypto.randomBytes(32).toString('hex');
    }

    // 哈希密码
    hashPassword(password, salt = null) {
        const useSalt = salt || this.generateSalt();
        const hash = crypto.pbkdf2Sync(password, useSalt, 10000, 64, 'sha512');
        return {
            hash: hash.toString('hex'),
            salt: useSalt
        };
    }

    // 验证密码
    verifyPassword(inputPassword, storedHash, salt) {
        const hash = crypto.pbkdf2Sync(inputPassword, salt, 10000, 64, 'sha512');
        return hash.toString('hex') === storedHash;
    }

    // 验证管理员凭据
    verifyAdminCredentials(username, password) {
        console.log('\n🔐 ===== 管理员认证验证 =====');
        console.log(`👤 用户名: ${username}`);
        
        const admin = this.config.admin;
        
        // 验证用户名
        if (username !== admin.username) {
            console.log('❌ 用户名不匹配');
            return {
                success: false,
                message: '用户名或密码错误'
            };
        }
        
        // 验证密码
        let isPasswordValid;
        if (typeof admin.passwordHash === 'string') {
            // 旧格式：直接存储哈希值
            isPasswordValid = this.verifyPassword(password, admin.passwordHash, admin.salt);
        } else {
            // 新格式：对象格式
            isPasswordValid = this.verifyPassword(password, admin.passwordHash.hash, admin.passwordHash.salt);
        }
        
        if (!isPasswordValid) {
            console.log('❌ 密码验证失败');
            return {
                success: false,
                message: '用户名或密码错误'
            };
        }
        
        console.log('✅ 管理员认证成功');
        console.log('🔐 ===== 认证验证完成 =====\n');
        
        return {
            success: true,
            message: '认证成功',
            user: {
                username: admin.username,
                role: 'admin'
            }
        };
    }

    // 修改管理员密码
    changeAdminPassword(currentPassword, newPassword) {
        console.log('\n🔐 ===== 修改管理员密码 =====');
        
        // 验证当前密码
        const verification = this.verifyAdminCredentials(this.config.admin.username, currentPassword);
        if (!verification.success) {
            console.log('❌ 当前密码验证失败');
            return {
                success: false,
                message: '当前密码错误'
            };
        }
        
        // 生成新密码哈希
        const newPasswordData = this.hashPassword(newPassword);
        
        // 更新配置
        this.config.admin.passwordHash = newPasswordData;
        this.config.admin.lastUpdate = new Date().toISOString();
        
        if (this.saveConfig()) {
            console.log('✅ 密码修改成功');
            console.log('🔐 ===== 密码修改完成 =====\n');
            return {
                success: true,
                message: '密码修改成功'
            };
        } else {
            console.log('❌ 密码修改失败：配置保存失败');
            return {
                success: false,
                message: '配置保存失败'
            };
        }
    }

    // 生成基本认证token
    generateBasicAuthToken(username, password) {
        const credentials = `${username}:${password}`;
        return Buffer.from(credentials).toString('base64');
    }

    // 解析基本认证token
    parseBasicAuthToken(token) {
        try {
            const credentials = Buffer.from(token, 'base64').toString();
            const [username, password] = credentials.split(':');
            return { username, password };
        } catch (error) {
            return null;
        }
    }
}

// 创建全局认证管理器实例
const authManager = new AuthManager();

/**
 * 管理员认证中间件
 * @param {Object} options - 配置选项
 * @param {string} options.source - 认证来源: 'headers', 'body'
 */
function requireAdminAuth(options = {}) {
    const { source = 'headers' } = options;
    
    return (req, res, next) => {
        console.log('\n🛡️  ===== 管理员认证检查开始 =====');
        console.log(`📍 请求路径: ${req.method} ${req.path}`);
        console.log(`📡 认证来源: ${source}`);
        
        try {
            let username, password;
            
            // 从不同来源获取认证数据
            console.log('\n🔍 步骤1: 获取认证数据');
            if (source === 'headers') {
                const authHeader = req.headers['authorization'];
                console.log(`📋 从Headers获取: ${authHeader ? '找到' : '未找到'}`);
                
                if (!authHeader) {
                    console.log('❌ 认证失败: 缺少Authorization头');
                    return res.status(401).json({
                        success: false,
                        message: '需要管理员认证',
                        errNo: 1,
                        errorMessage: 'Admin authentication required'
                    });
                }
                
                // 支持Basic认证
                if (authHeader.startsWith('Basic ')) {
                    const token = authHeader.substring(6);
                    const credentials = authManager.parseBasicAuthToken(token);
                    if (!credentials) {
                        console.log('❌ 认证失败: Basic认证格式错误');
                        return res.status(401).json({
                            success: false,
                            message: 'Basic认证格式错误',
                            errNo: 1,
                            errorMessage: 'Invalid Basic authentication format'
                        });
                    }
                    username = credentials.username;
                    password = credentials.password;
                    console.log('✅ Basic认证数据解析成功');
                } else {
                    console.log('❌ 认证失败: 不支持的认证类型');
                    return res.status(401).json({
                        success: false,
                        message: '不支持的认证类型',
                        errNo: 1,
                        errorMessage: 'Unsupported authentication type'
                    });
                }
            } else if (source === 'body') {
                const { username: bodyUsername, password: bodyPassword } = req.body;
                username = bodyUsername;
                password = bodyPassword;
                console.log(`📋 从Body获取: ${username && password ? '找到' : '未找到'}`);
            }
            
            if (!username || !password) {
                console.log('❌ 认证失败: 缺少用户名或密码');
                return res.status(401).json({
                    success: false,
                    message: '缺少用户名或密码',
                    errNo: 1,
                    errorMessage: 'Username and password required'
                });
            }
            
            // 验证管理员凭据
            console.log('\n🔍 步骤2: 验证管理员凭据');
            const result = authManager.verifyAdminCredentials(username, password);
            
            if (!result.success) {
                console.log('❌ 认证失败: 凭据验证失败');
                return res.status(401).json({
                    success: false,
                    message: result.message,
                    errNo: 1,
                    errorMessage: 'Invalid credentials'
                });
            }
            
            // 认证成功，将用户信息添加到请求对象
            req.authenticatedAdmin = result.user;
            console.log('\n🎉 ===== 管理员认证成功 =====');
            console.log(`✅ 认证通过: ${result.user.username} (${result.user.role})`);
            console.log('🛡️  ===== 认证检查完成 =====\n');
            
            next();
            
        } catch (error) {
            console.error('\n💥 ===== 管理员认证异常 =====');
            console.error('❌ 认证过程中发生错误:', error);
            console.error('🛡️  ===== 认证异常结束 =====\n');
            return res.status(500).json({
                success: false,
                message: '认证服务器错误',
                errNo: 1,
                errorMessage: 'Authentication server error'
            });
        }
    };
}

module.exports = {
    authManager,
    requireAdminAuth
}; 