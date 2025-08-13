@echo off
REM STM32 HBox 固件服务器部署脚本启动器
REM 使用方法: deploy.bat [环境名] 或 deploy.bat [环境] [服务器地址] [用户名] [部署路径]

setlocal enabledelayedexpansion

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

REM 设置PowerShell执行策略
powershell -Command "Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser -Force" >nul 2>&1

REM 执行PowerShell脚本
if "%1"=="" (
    echo [INFO] 启动部署脚本...
    powershell -ExecutionPolicy Bypass -File "%~dp0deploy.ps1"
) else (
    echo [INFO] 启动部署脚本，参数: %*
    powershell -ExecutionPolicy Bypass -File "%~dp0deploy.ps1" %*
)

REM 检查执行结果
if errorlevel 1 (
    echo [ERROR] 部署脚本执行失败
    pause
    exit /b 1
) else (
    echo [SUCCESS] 部署脚本执行完成
)

pause 