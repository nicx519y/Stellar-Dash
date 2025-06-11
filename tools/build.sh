#!/bin/bash
# STM32 H7xx 双槽构建工具 Linux/macOS Shell 脚本
# 这是 Python 构建工具的 Unix wrapper

set -e  # 遇到错误立即退出

# 获取脚本所在目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# 检查 Python 是否可用
if ! command -v python3 >/dev/null 2>&1; then
    echo "错误: 未找到 Python3。请确保 Python 3.7+ 已安装。"
    exit 1
fi

# 检查构建脚本是否存在
if [ ! -f "$SCRIPT_DIR/build.py" ]; then
    echo "错误: 构建脚本 build.py 不存在"
    exit 1
fi

# 调用 Python 构建脚本，传递所有参数
python3 "$SCRIPT_DIR/build.py" "$@" 