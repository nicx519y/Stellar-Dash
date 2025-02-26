#!/bin/bash

# 颜色定义
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 进度提示函数
progress() {
    echo -e "${BLUE}[进行中]${NC} $1"
}

success() {
    echo -e "${GREEN}[完成]${NC} $1"
}

warning() {
    echo -e "${YELLOW}[提示]${NC} $1"
}

# 目标服务器信息
REMOTE_HOST="root@81.70.40.123"
REMOTE_PATH="/root/hbox-webconfig"

# 创建临时目录
progress "正在创建临时目录..."
TEMP_DIR=$(mktemp -d)
success "临时目录创建完成: $TEMP_DIR"

# 复制所需文件到临时目录
progress "正在准备文件..."
rsync -a --progress \
    --exclude='.next' \
    --exclude='build' \
    --exclude='node_modules' \
    --exclude='next-env.d.ts' \
    --exclude='makefsdata.js' \
    --exclude='.DS_Store' \
    ./ "$TEMP_DIR/"
success "文件准备完成"

# 使用scp上传文件
warning "准备上传文件到远程服务器，请在提示时输入服务器密码..."
echo -e "${YELLOW}目标服务器:${NC} $REMOTE_HOST"
echo -e "${YELLOW}目标路径:${NC} $REMOTE_PATH"
echo "---------------------------------------------"
progress "正在上传文件到远程服务器..."
cd "$TEMP_DIR"
scp -r ./* "$REMOTE_HOST:$REMOTE_PATH/"
success "文件上传完成"

# 清理临时目录
progress "正在清理临时文件..."
cd - > /dev/null
rm -rf "$TEMP_DIR"
success "清理完成"

echo -e "\n${GREEN}部署完成！${NC}文件已上传到 $REMOTE_HOST:$REMOTE_PATH"
