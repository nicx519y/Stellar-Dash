#!/usr/bin/env node
// -*- coding: utf-8 -*-

/**
 * 设备认证模块
 * 
 * 功能:
 * 1. 设备签名生成和验证
 * 2. 设备认证中间件
 * 3. 挑战重放攻击防护
 */

const volidateDefaultConfig = {
    expiresIn: 2 * 60,   // 签名 有效期2分钟
}

/**
 * 生成设备签名 (与STM32端算法保持一致)
 */
function generateDeviceSignature(deviceId, challenge, timestamp) {
    console.log('\n🔐 ===== 设备签名生成过程 =====');
    console.log('📥 输入参数:');
    console.log(`  deviceId: "${deviceId}"`);
    console.log(`  challenge: "${challenge}"`);
    console.log(`  timestamp: ${timestamp}`);
    
    const signData = deviceId + challenge + timestamp.toString();
    console.log(`📝 签名数据拼接: "${signData}"`);
    console.log(`📏 签名数据长度: ${signData.length} 字符`);
    
    let hash = 0x9E3779B9;
    console.log(`🔢 初始哈希值: 0x${hash.toString(16).toUpperCase()}`);
    
    for (let i = 0; i < signData.length; i++) {
        const char = signData[i];
        const charCode = signData.charCodeAt(i);
        const oldHash = hash;
        
        hash = ((hash << 5) + hash) + charCode;
        hash = hash >>> 0; // 确保32位无符号整数
    }
    
    const finalSignature = `SIG_${hash.toString(16).toUpperCase().padStart(8, '0')}`;
    
    return finalSignature;
}

/**
 * 设备认证中间件
 * @param {Object} options - 配置选项
 * @param {string} options.source - token来源: 'headers', 'body', 'query'
 * @param {number} options.expiresIn - 过期时间(秒), 默认30分钟
 */
function validateDeviceAuth(options = {}) {
    options = { ...volidateDefaultConfig, ...options };
    const { source = 'headers', expiresIn } = options;
    
    return (req, res, next) => {
        console.log('\n🛡️  ===== 设备认证验证开始 =====');
        console.log(`📍 请求路径: ${req.method} ${req.path}`);
        console.log(`📡 认证来源: ${source}`);
        console.log(`⏰ 过期时间: ${expiresIn}秒 (${Math.round(expiresIn/60)}分钟)`);
        
        try {
            let authData;
            
            // 从不同来源获取认证数据
            console.log('\n🔍 步骤1: 获取认证数据');
            if (source === 'headers') {
                const authHeader = req.headers['x-device-auth'];
                console.log(`📋 从Headers获取: ${authHeader ? '找到' : '未找到'}`);
                if (!authHeader) {
                    console.log('❌ 认证失败: Headers中缺少认证信息');
                    return res.status(401).json({
                        error: 'AUTH_MISSING',
                        message: 'Device authentication required'
                    });
                }
                try {
                    authData = JSON.parse(Buffer.from(authHeader, 'base64').toString());
                    console.log('✅ Headers认证数据解析成功');
                } catch (e) {
                    console.log('❌ 认证失败: Headers认证数据格式错误');
                    return res.status(401).json({
                        error: 'AUTH_INVALID_FORMAT',
                        message: 'Invalid authentication format'
                    });
                }
            } else if (source === 'body') {
                authData = req.body.deviceAuth;
                console.log(`📋 从Body获取: ${authData ? '找到' : '未找到'}`);
                if (authData) {
                    console.log('✅ Body认证数据获取成功');
                } else {
                    console.log('❌ 认证失败: Body中缺少deviceAuth字段');
                }
            } else if (source === 'query') {
                const authParam = req.query.deviceAuth;
                console.log(`📋 从Query获取: ${authParam ? '找到' : '未找到'}`);
                if (authParam) {
                    try {
                        authData = JSON.parse(Buffer.from(authParam, 'base64').toString());
                        console.log('✅ Query认证数据解析成功');
                    } catch (e) {
                        console.log('❌ 认证失败: Query认证数据格式错误');
                        return res.status(401).json({
                            error: 'AUTH_INVALID_FORMAT',
                            message: 'Invalid authentication format'
                        });
                    }
                }
            }
            
            if (!authData) {
                console.log('❌ 认证失败: 未找到认证数据');
                return res.status(401).json({
                    error: 'AUTH_MISSING',
                    message: 'Device authentication data missing'
                });
            }
            
            // 验证必需字段
            console.log('\n🔍 步骤2: 验证认证数据字段');
            const { deviceId, challenge, timestamp, signature } = authData;
            console.log('📋 认证数据字段检查:');
            console.log(`  deviceId: ${deviceId ? '✅' : '❌'} "${deviceId || 'undefined'}"`);
            console.log(`  challenge: ${challenge ? '✅' : '❌'} "${challenge || 'undefined'}"`);
            console.log(`  timestamp: ${timestamp ? '✅' : '❌'} ${timestamp || 'undefined'}`);
            console.log(`  signature: ${signature ? '✅' : '❌'} "${signature || 'undefined'}"`);
            
            if (!deviceId || !challenge || !timestamp || !signature) {
                console.log('❌ 认证失败: 认证数据字段不完整');
                return res.status(401).json({
                    error: 'AUTH_INCOMPLETE',
                    message: 'Authentication data incomplete'
                });
            }
            console.log('✅ 认证数据字段完整');
            
            // 验证设备是否已注册 (需要从外部传入storage_manager)
            console.log('\n🔍 步骤3: 验证设备注册状态');
            if (!req.app.locals.storage_manager) {
                console.log('❌ 认证失败: 存储管理器未初始化');
                return res.status(500).json({
                    error: 'SERVER_ERROR',
                    message: 'Storage manager not initialized'
                });
            }
            
            const device = req.app.locals.storage_manager.findDevice(deviceId);
            if (!device) {
                console.log(`❌ 认证失败: 设备 "${deviceId}" 未注册`);
                return res.status(401).json({
                    error: 'DEVICE_NOT_REGISTERED',
                    message: 'Device not registered'
                });
            }
            console.log(`✅ 设备已注册: ${device.deviceName} (${device.deviceId})`);
            
            // 验证签名
            console.log('\n🔍 步骤4: 验证设备签名');
            console.log('🔐 开始生成期望签名...');
            const expectedSignature = generateDeviceSignature(deviceId, challenge, timestamp);
            
            console.log('📋 签名比较:');
            console.log(`  收到的签名: "${signature}"`);
            console.log(`  期望的签名: "${expectedSignature}"`);
            console.log(`  签名匹配: ${signature === expectedSignature ? '✅' : '❌'}`);
            
            if (signature !== expectedSignature) {
                console.log('❌ 认证失败: 设备签名验证失败');
                return res.status(401).json({
                    error: 'INVALID_SIGNATURE',
                    message: 'Invalid device signature'
                });
            }
            console.log('✅ 设备签名验证成功');
            
            // 基于挑战的重放攻击防护
            console.log('\n🔍 步骤5: 挑战重放攻击防护');
            // 存储挑战首次使用时间，允许在有效期内重复使用
            if (!global.usedChallenges) {
                global.usedChallenges = new Map();
                console.log('🆕 初始化挑战存储Map');
            }
            
            console.log(`📋 当前已记录的挑战数量: ${global.usedChallenges.size}`);
            
            // 检查挑战是否已被使用过
            const challengeFirstUsed = global.usedChallenges.get(challenge);
            if (challengeFirstUsed) {
                // 如果挑战已被使用，检查是否超出有效期
                const timeSinceFirstUse = Date.now() - challengeFirstUsed;
                console.log(`🔄 挑战已存在: "${challenge}"`);
                console.log(`⏰ 首次使用时间: ${new Date(challengeFirstUsed).toISOString()}`);
                console.log(`⏰ 已使用时长: ${Math.round(timeSinceFirstUse / 1000)}秒`);
                console.log(`⏰ 有效期限制: ${expiresIn}秒`);
                
                if (timeSinceFirstUse > (expiresIn * 1000)) {
                    console.log('❌ 认证失败: 挑战已过期');
                    return res.status(401).json({
                        error: 'CHALLENGE_EXPIRED',
                        message: 'Challenge has expired'
                    });
                }
                // 在有效期内，允许重复使用
                console.log(`✅ 挑战复用成功: ${challenge}, 已使用 ${Math.round(timeSinceFirstUse / 1000)}秒`);
            } else {
                // 第一次使用此挑战，记录使用时间
                global.usedChallenges.set(challenge, Date.now());
                console.log(`🆕 挑战首次使用: "${challenge}"`);
                console.log(`⏰ 记录时间: ${new Date().toISOString()}`);
            }
            
            // 定期清理过期的挑战记录（保留最近30分钟的记录）
            const cleanupThreshold = Date.now() - (expiresIn * 1000);
            let cleanupCount = 0;
            for (const [usedChallenge, usedTime] of global.usedChallenges.entries()) {
                if (usedTime < cleanupThreshold) {
                    global.usedChallenges.delete(usedChallenge);
                    cleanupCount++;
                }
            }
            if (cleanupCount > 0) {
                console.log(`🧹 清理过期挑战: ${cleanupCount}个, 剩余: ${global.usedChallenges.size}个`);
            }
            
            // 验证通过，将设备信息添加到请求对象
            req.authenticatedDevice = device;
            console.log('\n🎉 ===== 设备认证验证成功 =====');
            console.log(`✅ 认证通过: ${device.deviceName} (${device.deviceId})`);
            console.log(`🔐 使用的挑战: "${challenge}"`);
            console.log('🛡️  ===== 认证验证完成 =====\n');
            
            next();
            
        } catch (error) {
            console.error('\n💥 ===== 设备认证验证异常 =====');
            console.error('❌ 认证过程中发生错误:', error);
            console.error('🛡️  ===== 认证验证异常结束 =====\n');
            return res.status(500).json({
                error: 'AUTH_SERVER_ERROR',
                message: 'Authentication server error'
            });
        }
    };
}

module.exports = {
    generateDeviceSignature,
    validateDeviceAuth
}; 