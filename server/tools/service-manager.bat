@echo off
REM STM32 HBox 固件服务器服务管理脚本启动器
REM 使用方法: service-manager.bat [环境名] [命令]

setlocal enabledelayedexpansion

REM 检查PowerShell是否可用
powershell -Command "exit" >nul 2>&1
if errorlevel 1 (
    echo [ERROR] PowerShell不可用，请确保PowerShell已安装
    pause
    exit /b 1
)

REM 检查脚本文件是否存在
if not exist "%~dp0service-manager.ps1" (
    echo [ERROR] 未找到service-manager.ps1脚本文件
    pause
    exit /b 1
)

REM 检查参数
if "%1"=="" (
    echo [ERROR] 请提供环境名参数
    echo 使用方法: service-manager.bat [环境名] [命令]
    echo 命令: status, start, stop, restart, reload, logs, monitor, backup, health
    pause
    exit /b 1
)

if "%2"=="" (
    echo [ERROR] 请提供命令参数
    echo 使用方法: service-manager.bat [环境名] [命令]
    echo 命令: status, start, stop, restart, reload, logs, monitor, backup, health
    pause
    exit /b 1
)

REM 设置PowerShell执行策略
powershell -Command "Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser -Force" >nul 2>&1

REM 执行PowerShell脚本
echo [INFO] 启动服务管理脚本，环境: %1，命令: %2
powershell -ExecutionPolicy Bypass -File "%~dp0service-manager.ps1" %1 %2

REM 检查执行结果
if errorlevel 1 (
    echo [ERROR] 服务管理脚本执行失败
    pause
    exit /b 1
) else (
    echo [SUCCESS] 服务管理脚本执行完成
)

pause 