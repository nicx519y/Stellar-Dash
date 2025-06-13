@echo off
chcp 65001 >nul
echo ============================================================
echo STM32 HBox 固件管理服务器 - Windows启动脚本
echo ============================================================

REM 检查Node.js是否安装
node --version >nul 2>&1
if errorlevel 1 (
    echo ❌ 错误: 未安装Node.js或Node.js不在PATH中
    echo 请下载并安装Node.js: https://nodejs.org/
    pause
    exit /b 1
)

echo ✅ 发现Node.js，正在启动服务器...
echo.

REM 启动服务器
node start.js

pause 