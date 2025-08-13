@echo off
REM STM32 HBox 固件服务器快速服务管理脚本
REM 提供交互式服务管理选择

setlocal enabledelayedexpansion

echo ========================================
echo    STM32 HBox 固件服务器服务管理
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
if not exist "%~dp0service-manager.ps1" (
    echo [ERROR] 未找到service-manager.ps1脚本文件
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

echo 请选择环境:
echo.
echo 1. 开发环境 (dev)
echo 2. 测试环境 (test)  
echo 3. 生产环境 (prod)
echo 4. 退出
echo.

set /p env_choice="请输入环境选择 (1-4): "

if "%env_choice%"=="1" (
    set ENV=dev
) else if "%env_choice%"=="2" (
    set ENV=test
) else if "%env_choice%"=="3" (
    set ENV=prod
) else if "%env_choice%"=="4" (
    echo [INFO] 退出服务管理工具
    exit /b 0
) else (
    echo [ERROR] 无效选择，请输入 1-4
    pause
    exit /b 1
)

echo.
echo 请选择操作:
echo.
echo 1. 查看服务状态
echo 2. 启动服务
echo 3. 停止服务
echo 4. 重启服务
echo 5. 查看日志
echo 6. 健康检查
echo 7. 备份数据
echo 8. 监控服务
echo 9. 返回环境选择
echo 10. 退出
echo.

set /p cmd_choice="请输入操作选择 (1-10): "

if "%cmd_choice%"=="1" (
    echo.
    echo [INFO] 查看服务状态...
    powershell -ExecutionPolicy Bypass -File "%~dp0service-manager.ps1" %ENV% status
) else if "%cmd_choice%"=="2" (
    echo.
    echo [INFO] 启动服务...
    powershell -ExecutionPolicy Bypass -File "%~dp0service-manager.ps1" %ENV% start
) else if "%cmd_choice%"=="3" (
    echo.
    echo [INFO] 停止服务...
    powershell -ExecutionPolicy Bypass -File "%~dp0service-manager.ps1" %ENV% stop
) else if "%cmd_choice%"=="4" (
    echo.
    echo [INFO] 重启服务...
    powershell -ExecutionPolicy Bypass -File "%~dp0service-manager.ps1" %ENV% restart
) else if "%cmd_choice%"=="5" (
    echo.
    echo [INFO] 查看日志...
    powershell -ExecutionPolicy Bypass -File "%~dp0service-manager.ps1" %ENV% logs
) else if "%cmd_choice%"=="6" (
    echo.
    echo [INFO] 健康检查...
    powershell -ExecutionPolicy Bypass -File "%~dp0service-manager.ps1" %ENV% health
) else if "%cmd_choice%"=="7" (
    echo.
    echo [INFO] 备份数据...
    powershell -ExecutionPolicy Bypass -File "%~dp0service-manager.ps1" %ENV% backup
) else if "%cmd_choice%"=="8" (
    echo.
    echo [INFO] 监控服务...
    powershell -ExecutionPolicy Bypass -File "%~dp0service-manager.ps1" %ENV% monitor
) else if "%cmd_choice%"=="9" (
    echo.
    goto :start
) else if "%cmd_choice%"=="10" (
    echo [INFO] 退出服务管理工具
    exit /b 0
) else (
    echo [ERROR] 无效选择，请输入 1-10
)

echo.
pause
goto :start

:start
echo.
echo ========================================
echo    STM32 HBox 固件服务器服务管理
echo ========================================
echo.

echo 请选择环境:
echo.
echo 1. 开发环境 (dev)
echo 2. 测试环境 (test)  
echo 3. 生产环境 (prod)
echo 4. 退出
echo.

set /p env_choice="请输入环境选择 (1-4): "

if "%env_choice%"=="1" (
    set ENV=dev
) else if "%env_choice%"=="2" (
    set ENV=test
) else if "%env_choice%"=="3" (
    set ENV=prod
) else if "%env_choice%"=="4" (
    echo [INFO] 退出服务管理工具
    exit /b 0
) else (
    echo [ERROR] 无效选择，请输入 1-4
    pause
    exit /b 1
)

echo.
echo 请选择操作:
echo.
echo 1. 查看服务状态
echo 2. 启动服务
echo 3. 停止服务
echo 4. 重启服务
echo 5. 查看日志
echo 6. 健康检查
echo 7. 备份数据
echo 8. 监控服务
echo 9. 返回环境选择
echo 10. 退出
echo.

set /p cmd_choice="请输入操作选择 (1-10): "

if "%cmd_choice%"=="1" (
    echo.
    echo [INFO] 查看服务状态...
    powershell -ExecutionPolicy Bypass -File "%~dp0service-manager.ps1" %ENV% status
) else if "%cmd_choice%"=="2" (
    echo.
    echo [INFO] 启动服务...
    powershell -ExecutionPolicy Bypass -File "%~dp0service-manager.ps1" %ENV% start
) else if "%cmd_choice%"=="3" (
    echo.
    echo [INFO] 停止服务...
    powershell -ExecutionPolicy Bypass -File "%~dp0service-manager.ps1" %ENV% stop
) else if "%cmd_choice%"=="4" (
    echo.
    echo [INFO] 重启服务...
    powershell -ExecutionPolicy Bypass -File "%~dp0service-manager.ps1" %ENV% restart
) else if "%cmd_choice%"=="5" (
    echo.
    echo [INFO] 查看日志...
    powershell -ExecutionPolicy Bypass -File "%~dp0service-manager.ps1" %ENV% logs
) else if "%cmd_choice%"=="6" (
    echo.
    echo [INFO] 健康检查...
    powershell -ExecutionPolicy Bypass -File "%~dp0service-manager.ps1" %ENV% health
) else if "%cmd_choice%"=="7" (
    echo.
    echo [INFO] 备份数据...
    powershell -ExecutionPolicy Bypass -File "%~dp0service-manager.ps1" %ENV% backup
) else if "%cmd_choice%"=="8" (
    echo.
    echo [INFO] 监控服务...
    powershell -ExecutionPolicy Bypass -File "%~dp0service-manager.ps1" %ENV% monitor
) else if "%cmd_choice%"=="9" (
    echo.
    goto :start
) else if "%cmd_choice%"=="10" (
    echo [INFO] 退出服务管理工具
    exit /b 0
) else (
    echo [ERROR] 无效选择，请输入 1-10
)

echo.
pause
goto :start 