@echo off
chcp 65001 >nul
echo ============================================================
echo STM32 HBox 固件管理服务器 - Windows停止脚本
echo ============================================================

REM 检查命令行参数
set "command=%1"
if "%command%"=="" set "command=stop"

echo 执行命令: %command%
echo.

REM 停止服务器
node stop.js %command%

pause 