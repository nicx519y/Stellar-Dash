@echo off
REM STM32 HBox 固件服务器快速部署脚本
REM 提供交互式环境选择

setlocal enabledelayedexpansion

echo ========================================
echo    STM32 HBox 固件服务器快速部署
echo ========================================
echo.

REM 检查PowerShell是否可用
powershell -Command "exit" >nul 2>&1
if errorlevel 1 (
    echo [ERROR] PowerShell不可用，请确保PowerShell已安装
    pause
    exit /b 1
)

REM 检查脚本文件是否存在
if not exist "%~dp0deploy.ps1" (
    echo [ERROR] 未找到deploy.ps1脚本文件
    pause
    exit /b 1
)

REM 检查配置文件是否存在
if not exist "%~dp0deploy-config.json" (
    echo [ERROR] 未找到deploy-config.json配置文件
    echo 请先运行 setup.ps1 创建配置文件
    pause
    exit /b 1
)

echo 请选择部署环境:
echo.
echo 1. 开发环境 (dev)
echo 2. 测试环境 (test)  
echo 3. 生产环境 (prod)
echo 4. 测试连接
echo 5. 退出
echo.

set /p choice="请输入选择 (1-5): "

if "%choice%"=="1" (
    echo.
    echo [INFO] 部署到开发环境...
    powershell -ExecutionPolicy Bypass -File "%~dp0deploy.ps1" dev
) else if "%choice%"=="2" (
    echo.
    echo [INFO] 部署到测试环境...
    powershell -ExecutionPolicy Bypass -File "%~dp0deploy.ps1" test
) else if "%choice%"=="3" (
    echo.
    echo [INFO] 部署到生产环境...
    powershell -ExecutionPolicy Bypass -File "%~dp0deploy.ps1" prod
) else if "%choice%"=="4" (
    echo.
    echo [INFO] 测试连接...
    echo 请选择要测试的环境:
    echo 1. 开发环境 (dev)
    echo 2. 测试环境 (test)
    echo 3. 生产环境 (prod)
    echo.
    set /p test_choice="请输入选择 (1-3): "
    
    if "%test_choice%"=="1" (
        powershell -ExecutionPolicy Bypass -File "%~dp0test-connection.ps1" dev
    ) else if "%test_choice%"=="2" (
        powershell -ExecutionPolicy Bypass -File "%~dp0test-connection.ps1" test
    ) else if "%test_choice%"=="3" (
        powershell -ExecutionPolicy Bypass -File "%~dp0test-connection.ps1" prod
    ) else (
        echo [ERROR] 无效选择
    )
) else if "%choice%"=="5" (
    echo [INFO] 退出部署工具
    exit /b 0
) else (
    echo [ERROR] 无效选择，请输入 1-5
)

echo.
pause 