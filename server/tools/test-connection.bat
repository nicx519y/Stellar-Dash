@echo off
REM STM32 HBox 固件服务器连接测试脚本启动器
REM 使用方法: test-connection.bat [环境名]

setlocal enabledelayedexpansion

REM 检查PowerShell是否可用
powershell -Command "exit" >nul 2>&1
if errorlevel 1 (
    echo [ERROR] PowerShell不可用，请确保PowerShell已安装
    pause
    exit /b 1
)

REM 检查脚本文件是否存在
if not exist "%~dp0test-connection.ps1" (
    echo [ERROR] 未找到test-connection.ps1脚本文件
    pause
    exit /b 1
)

REM 检查参数
if "%1"=="" (
    echo [ERROR] 请提供环境名参数
    echo 使用方法: test-connection.bat [环境名]
    echo 环境名: dev, test, prod
    pause
    exit /b 1
)

REM 设置PowerShell执行策略
powershell -Command "Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser -Force" >nul 2>&1

REM 执行PowerShell脚本
echo [INFO] 启动连接测试脚本，环境: %1
powershell -ExecutionPolicy Bypass -File "%~dp0test-connection.ps1" %1

REM 检查执行结果
if errorlevel 1 (
    echo [ERROR] 连接测试脚本执行失败
    pause
    exit /b 1
) else (
    echo [SUCCESS] 连接测试脚本执行完成
)

pause 