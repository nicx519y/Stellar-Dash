#!/usr/bin/env node
// -*- coding: utf-8 -*-

/**
 * STM32 HBox 固件服务器启动脚本
 */

const { spawn } = require('child_process');
const fs = require('fs');
const path = require('path');

// 检查Node.js版本
function checkNodeVersion() {
    const version = process.version;
    const majorVersion = parseInt(version.slice(1).split('.')[0]);
    
    if (majorVersion < 14) {
        console.error('❌ 错误: 需要 Node.js 14.0.0 或更高版本');
        console.error(`当前版本: ${version}`);
        process.exit(1);
    }
    
    console.log(`✅ Node.js版本检查通过: ${version}`);
}

// 检查依赖是否已安装
function checkDependencies() {
    const packageJsonPath = path.join(__dirname, 'package.json');
    const nodeModulesPath = path.join(__dirname, 'node_modules');
    
    if (!fs.existsSync(packageJsonPath)) {
        console.error('❌ 错误: 未找到 package.json 文件');
        process.exit(1);
    }
    
    if (!fs.existsSync(nodeModulesPath)) {
        console.log('📦 依赖尚未安装，正在安装...');
        return false;
    }
    
    console.log('✅ 依赖检查通过');
    return true;
}

// 安装依赖
function installDependencies() {
    return new Promise((resolve, reject) => {
        console.log('正在安装依赖包...');
        
        const npm = spawn('npm', ['install'], {
            cwd: __dirname,
            stdio: 'inherit'
        });
        
        npm.on('close', (code) => {
            if (code === 0) {
                console.log('✅ 依赖安装完成');
                resolve();
            } else {
                console.error('❌ 依赖安装失败');
                reject(new Error(`npm install failed with code ${code}`));
            }
        });
        
        npm.on('error', (error) => {
            console.error('❌ 启动npm失败:', error.message);
            reject(error);
        });
    });
}

// 启动服务器
function startServer() {
    console.log('\n🚀 正在启动 STM32 HBox 固件管理服务器...\n');
    
    const server = spawn('node', ['server.js'], {
        cwd: __dirname,
        stdio: 'inherit'
    });
    
    server.on('error', (error) => {
        console.error('❌ 启动服务器失败:', error.message);
        process.exit(1);
    });
    
    server.on('close', (code) => {
        if (code !== 0) {
            console.error(`❌ 服务器异常退出，退出码: ${code}`);
            process.exit(code);
        }
    });
    
    // 处理中断信号
    process.on('SIGINT', () => {
        console.log('\n🛑 收到中断信号，正在关闭服务器...');
        server.kill('SIGINT');
        process.exit(0);
    });
    
    process.on('SIGTERM', () => {
        console.log('\n🛑 收到终止信号，正在关闭服务器...');
        server.kill('SIGTERM');
        process.exit(0);
    });
}

// 主函数
async function main() {
    try {
        console.log('='.repeat(60));
        console.log('STM32 HBox 固件管理服务器 - 启动脚本');
        console.log('='.repeat(60));
        
        // 检查环境
        checkNodeVersion();
        
        // 检查和安装依赖
        if (!checkDependencies()) {
            await installDependencies();
        }
        
        // 启动服务器
        startServer();
        
    } catch (error) {
        console.error('❌ 启动失败:', error.message);
        process.exit(1);
    }
}

// 如果直接运行此脚本
if (require.main === module) {
    main();
}

module.exports = { main }; 