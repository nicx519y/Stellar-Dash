#!/usr/bin/env node
// -*- coding: utf-8 -*-

/**
 * STM32 HBox 固件服务器停止脚本
 */

const { exec } = require('child_process');
const os = require('os');

// 根据操作系统查找并停止服务器进程
function stopServer() {
    console.log('🛑 正在停止 STM32 HBox 固件管理服务器...');
    
    const platform = os.platform();
    let command;
    
    if (platform === 'win32') {
        // Windows系统
        command = 'taskkill /F /IM node.exe /FI "WINDOWTITLE eq *server.js*"';
    } else {
        // Unix-like系统 (Linux, macOS)
        command = 'pkill -f "node.*server.js"';
    }
    
    exec(command, (error, stdout, stderr) => {
        if (error) {
            // 如果没有找到进程，也算成功
            if (error.code === 1 || error.message.includes('not found') || error.message.includes('没有找到')) {
                console.log('ℹ️  服务器进程未运行或已停止');
                return;
            }
            console.error('❌ 停止服务器失败:', error.message);
            process.exit(1);
        }
        
        if (stdout) {
            console.log(stdout);
        }
        
        if (stderr && !stderr.includes('not found') && !stderr.includes('没有找到')) {
            console.log('⚠️  警告:', stderr);
        }
        
        console.log('✅ 服务器已停止');
    });
}

// 优雅停止 - 尝试发送SIGTERM信号
function gracefulStop() {
    console.log('🛑 正在优雅停止 STM32 HBox 固件管理服务器...');
    
    const platform = os.platform();
    let command;
    
    if (platform === 'win32') {
        // Windows下没有SIGTERM，直接强制停止
        stopServer();
        return;
    }
    
    // Unix-like系统先尝试SIGTERM
    command = 'pkill -TERM -f "node.*server.js"';
    
    exec(command, (error, stdout, stderr) => {
        if (error) {
            if (error.code === 1) {
                console.log('ℹ️  服务器进程未运行');
                return;
            }
            console.log('⚠️  SIGTERM失败，尝试强制停止...');
            stopServer();
            return;
        }
        
        console.log('✅ 发送停止信号成功，等待服务器优雅关闭...');
        
        // 等待3秒后检查是否还在运行
        setTimeout(() => {
            exec('pgrep -f "node.*server.js"', (error) => {
                if (error) {
                    // 进程不存在，说明已经停止
                    console.log('✅ 服务器已优雅停止');
                } else {
                    // 进程仍然存在，强制停止
                    console.log('⚠️  服务器未响应停止信号，强制停止...');
                    stopServer();
                }
            });
        }, 3000);
    });
}

// 检查服务器状态
function checkStatus() {
    console.log('🔍 检查服务器状态...');
    
    const platform = os.platform();
    let command;
    
    if (platform === 'win32') {
        command = 'tasklist /FI "IMAGENAME eq node.exe" /FO CSV';
    } else {
        command = 'pgrep -f "node.*server.js"';
    }
    
    exec(command, (error, stdout, stderr) => {
        if (error) {
            console.log('ℹ️  服务器未运行');
            return;
        }
        
        if (platform === 'win32') {
            // Windows下解析tasklist输出
            const lines = stdout.split('\n');
            const nodeProcesses = lines.filter(line => line.includes('node.exe')).length - 1; // 减去标题行
            if (nodeProcesses > 0) {
                console.log(`ℹ️  发现 ${nodeProcesses} 个 Node.js 进程正在运行`);
            } else {
                console.log('ℹ️  服务器未运行');
            }
        } else {
            // Unix-like系统
            const pids = stdout.trim().split('\n').filter(pid => pid);
            if (pids.length > 0) {
                console.log(`ℹ️  服务器正在运行 (PID: ${pids.join(', ')})`);
            } else {
                console.log('ℹ️  服务器未运行');
            }
        }
    });
}

// 主函数
function main() {
    const args = process.argv.slice(2);
    const command = args[0] || 'stop';
    
    console.log('='.repeat(60));
    console.log('STM32 HBox 固件管理服务器 - 停止脚本');
    console.log('='.repeat(60));
    
    switch (command) {
        case 'stop':
        case 'kill':
            stopServer();
            break;
        case 'graceful':
        case 'graceful-stop':
            gracefulStop();
            break;
        case 'status':
        case 'check':
            checkStatus();
            break;
        case 'restart':
            console.log('🔄 重启服务器...');
            gracefulStop();
            setTimeout(() => {
                console.log('🚀 启动服务器...');
                require('./start').main();
            }, 5000);
            break;
        default:
            console.log('用法:');
            console.log('  node stop.js [command]');
            console.log('');
            console.log('命令:');
            console.log('  stop, kill      - 强制停止服务器 (默认)');
            console.log('  graceful        - 优雅停止服务器');
            console.log('  status, check   - 检查服务器状态');
            console.log('  restart         - 重启服务器');
            break;
    }
}

// 如果直接运行此脚本
if (require.main === module) {
    main();
}

module.exports = { main, stopServer, gracefulStop, checkStatus }; 