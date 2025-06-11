@echo off
:: STM32 H7xx 双槽构建工具 Windows 批处理脚本
:: 这是 Python 构建工具的 Windows wrapper

setlocal enabledelayedexpansion

:: 获取脚本所在目录
set SCRIPT_DIR=%~dp0
set PROJECT_ROOT=%SCRIPT_DIR%..

:: 检查 Python 是否可用
python --version >nul 2>&1
if %errorlevel% neq 0 (
    echo 错误: 未找到 Python。请确保 Python 已安装并添加到 PATH 中。
    echo 推荐使用 Python 3.7 或更高版本。
    exit /b 1
)

:: 检查构建脚本是否存在
if not exist "%SCRIPT_DIR%build.py" (
    echo 错误: 构建脚本 build.py 不存在
    exit /b 1
)

:: 调用 Python 构建脚本，传递所有参数
python "%SCRIPT_DIR%build.py" %*

:: 保持错误码
exit /b %errorlevel% 