# STM32 HBox 固件服务器部署脚本 (PowerShell版本)
# 使用方法: .\deploy.ps1 [环境名] 或 .\deploy.ps1 [环境] [服务器地址] [用户名] [部署路径]

param(
    [string]$Environment = "",
    [string]$ServerHost = "",
    [string]$User = "",
    [string]$DeployPath = ""
)

# 设置错误处理
$ErrorActionPreference = "Stop"

# 颜色定义
$Red = "Red"
$Green = "Green"
$Yellow = "Yellow"
$Blue = "Blue"
$White = "White"

# 日志函数
function Write-Info {
    param([string]$Message)
    Write-Host "[INFO] $Message" -ForegroundColor $Blue
}

function Write-Success {
    param([string]$Message)
    Write-Host "[SUCCESS] $Message" -ForegroundColor $Green
}

function Write-Warning {
    param([string]$Message)
    Write-Host "[WARNING] $Message" -ForegroundColor $Yellow
}

function Write-Error {
    param([string]$Message)
    Write-Host "[ERROR] $Message" -ForegroundColor $Red
}

# 显示帮助信息
function Show-Help {
    Write-Host "STM32 HBox 固件服务器部署脚本 (PowerShell版本)" -ForegroundColor $White
    Write-Host ""
    Write-Host "使用方法:" -ForegroundColor $White
    Write-Host "  .\deploy.ps1 [环境名]" -ForegroundColor $White
    Write-Host "  .\deploy.ps1 [环境] [服务器地址] [用户名] [部署路径]" -ForegroundColor $White
    Write-Host ""
    Write-Host "参数说明:" -ForegroundColor $White
    Write-Host "  环境名       - 使用配置文件中的环境 (dev/test/prod)" -ForegroundColor $White
    Write-Host "  环境         - 部署环境 (dev/test/prod)" -ForegroundColor $White
    Write-Host "  服务器地址   - 远程服务器IP或域名" -ForegroundColor $White
    Write-Host "  用户名       - SSH用户名" -ForegroundColor $White
    Write-Host "  部署路径     - 远程服务器上的部署路径" -ForegroundColor $White
    Write-Host ""
    Write-Host "示例:" -ForegroundColor $White
    Write-Host "  .\deploy.ps1 prod" -ForegroundColor $White
    Write-Host "  .\deploy.ps1 prod 192.168.1.100 root /opt/hbox-server" -ForegroundColor $White
    Write-Host ""
    Write-Host "配置文件: $ConfigFile" -ForegroundColor $White
}

# 检查必需的工具
function Test-Requirements {
    Write-Info "检查部署环境..."
    
    # 检查PowerShell版本
    if ($PSVersionTable.PSVersion.Major -lt 5) {
        Write-Error "需要PowerShell 5.0或更高版本"
        exit 1
    }
    
    # 检查SSH密钥文件
    if (-not (Test-Path $SshKey)) {
        Write-Error "SSH密钥文件不存在: $SshKey"
        exit 1
    }
    
    # 设置.pem文件权限 (仅适用于WSL)
    if ($UseWsl -and $SshKey -match "\.pem$") {
        Write-Info "设置.pem密钥文件权限..."
        try {
            wsl chmod 600 $SshKey
            Write-Success "密钥文件权限设置完成"
        }
        catch {
            Write-Warning "无法设置密钥文件权限，可能影响SSH连接"
        }
    }
    
    # 检查SSH命令 (通过WSL或Git Bash)
    $sshPath = Get-Command "ssh" -ErrorAction SilentlyContinue
    if (-not $sshPath) {
        # 尝试查找WSL中的SSH
        $wslSsh = "wsl ssh"
        try {
            & wsl ssh -V 2>$null
            $script:UseWsl = $true
            Write-Info "使用WSL SSH"
        }
        catch {
            Write-Error "未找到SSH命令，请安装以下任一工具:"
            Write-Info "1. Windows Subsystem for Linux (WSL)"
            Write-Info "2. Git for Windows (包含Git Bash)"
            Write-Info "3. OpenSSH for Windows"
            exit 1
        }
    }
    
    Write-Success "部署环境检查通过"
}

# 加载配置文件
function Load-Config {
    param([string]$EnvName)
    
    if (-not (Test-Path $ConfigFile)) {
        Write-Error "配置文件不存在: $ConfigFile"
        exit 1
    }
    
    try {
        $config = Get-Content $ConfigFile | ConvertFrom-Json
        
        if (-not $config.environments.$EnvName) {
            Write-Error "未找到环境配置: $EnvName"
            Show-Help
            exit 1
        }
        
        $envConfig = $config.environments.$EnvName
        
        $script:ServerHost = $envConfig.host
        $script:User = $envConfig.user
        $script:DeployPath = $envConfig.path
        $script:SshKey = $envConfig.ssh_key
        $script:Port = $envConfig.port
        $script:Description = $envConfig.description
        
        Write-Info "部署配置:"
        Write-Info "  环境: $EnvName ($Description)"
        Write-Info "  服务器: $User@$ServerHost"
        Write-Info "  路径: $DeployPath"
        Write-Info "  端口: $Port"
        Write-Info "  SSH密钥: $SshKey"
        
    }
    catch {
        Write-Error "加载配置文件失败: $($_.Exception.Message)"
        exit 1
    }
}

# 验证参数
function Test-Parameters {
    if ($Environment -eq "-h" -or $Environment -eq "--help") {
        Show-Help
        exit 0
    }
    
    # 如果只提供了环境名，使用配置文件
    if ($Environment -and -not $ServerHost) {
        Load-Config $Environment
    }
    # 如果提供了完整参数，验证必需参数
    elseif ($ServerHost -and $User -and $DeployPath) {
        $script:SshKey = "~/.ssh/id_rsa"
        $script:Port = 3000
        $script:Description = "自定义环境"
        
        Write-Info "部署配置:"
        Write-Info "  环境: $Environment"
        Write-Info "  服务器: $User@$ServerHost"
        Write-Info "  路径: $DeployPath"
        Write-Info "  端口: $Port"
        Write-Info "  SSH密钥: $SshKey"
    }
    else {
        Write-Error "参数不足"
        Show-Help
        exit 1
    }
    
    # 验证环境参数
    if ($Environment -and $Environment -notmatch "^(dev|test|prod)$") {
        Write-Error "环境参数必须是 dev、test 或 prod"
        exit 1
    }
}

# 创建临时目录
function New-TempDirectory {
    $script:TempDir = [System.IO.Path]::GetTempPath() + [System.Guid]::NewGuid().ToString()
    $script:PackageName = "hbox-server-$(Get-Date -Format 'yyyyMMdd-HHmmss').zip"
    $script:PackagePath = Join-Path $TempDir $PackageName
    
    New-Item -ItemType Directory -Path $TempDir -Force | Out-Null
    Write-Info "创建临时目录: $TempDir"
}

# 打包服务器文件
function New-ServerPackage {
    Write-Info "开始打包服务器文件..."
    
    # 切换到服务器目录
    $serverDir = Split-Path $PSScriptRoot
    Set-Location $serverDir
    
    # 创建打包目录
    $packageDir = Join-Path $TempDir "hbox-server"
    New-Item -ItemType Directory -Path $packageDir -Force | Out-Null
    
    # 复制必需的文件和目录
    Write-Info "复制核心文件..."
    Copy-Item -Path "src" -Destination $packageDir -Recurse -Force
    Copy-Item -Path "data" -Destination $packageDir -Recurse -Force
    Copy-Item -Path "package.json" -Destination $packageDir -Force
    Copy-Item -Path "package-lock.json" -Destination $packageDir -Force
    Copy-Item -Path "start.js" -Destination $packageDir -Force
    Copy-Item -Path "stop.js" -Destination $packageDir -Force
    
    # 复制工具目录（除了部署脚本本身）
    if (Test-Path "tools") {
        $toolsDir = Join-Path $packageDir "tools"
        New-Item -ItemType Directory -Path $toolsDir -Force | Out-Null
        Get-ChildItem "tools" -File | Where-Object { $_.Name -ne "deploy.ps1" } | ForEach-Object {
            Copy-Item $_.FullName -Destination $toolsDir -Force
        }
    }
    
    # 创建uploads目录
    $uploadsDir = Join-Path $packageDir "uploads"
    New-Item -ItemType Directory -Path $uploadsDir -Force | Out-Null
    
    # 创建环境配置文件
    New-EnvConfig $packageDir
    
    # 创建PM2配置文件
    New-Pm2Config $packageDir
    
    # 创建远程部署脚本
    New-RemoteDeployScript $packageDir
    
    # 打包
    Set-Location $TempDir
    try {
        # 使用PowerShell内置的Compress-Archive命令
        Compress-Archive -Path "hbox-server\*" -DestinationPath $PackageName -Force
        Write-Success "打包完成: $PackagePath"
    }
    catch {
        Write-Error "打包失败: $($_.Exception.Message)"
        exit 1
    }
}

# 创建环境配置文件
function New-EnvConfig {
    param([string]$PackageDir)
    
    Write-Info "创建环境配置文件..."
    
    $envContent = @"
# STM32 HBox 固件服务器环境配置
NODE_ENV=$Environment
PORT=3000

# 数据库配置
DB_HOST=localhost
DB_PORT=3306
DB_NAME=hbox_firmware
DB_USER=hbox_user
DB_PASSWORD=your_password_here

# 文件上传配置
UPLOAD_DIR=./uploads
MAX_FILE_SIZE=50MB

# 安全配置
JWT_SECRET=your_jwt_secret_here
SESSION_SECRET=your_session_secret_here

# 日志配置
LOG_LEVEL=info
LOG_FILE=./logs/server.log

# 服务器配置
HOST=0.0.0.0
CORS_ORIGIN=*
"@
    
    $envContent | Out-File -FilePath (Join-Path $PackageDir ".env") -Encoding UTF8
    Write-Success "环境配置文件已创建"
}

# 创建PM2配置文件
function New-Pm2Config {
    param([string]$PackageDir)
    
    Write-Info "创建PM2配置文件..."
    
    $pm2Content = @"
module.exports = {
  apps: [{
    name: 'hbox-firmware-server',
    script: './src/server.js',
    cwd: __dirname,
    instances: 1,
    autorestart: true,
    watch: false,
    max_memory_restart: '1G',
    env: {
      NODE_ENV: '$Environment',
      PORT: 3000
    },
    env_production: {
      NODE_ENV: 'production',
      PORT: 3000
    },
    error_file: './logs/err.log',
    out_file: './logs/out.log',
    log_file: './logs/combined.log',
    time: true,
    log_date_format: 'YYYY-MM-DD HH:mm:ss Z'
  }]
};
"@
    
    $pm2Content | Out-File -FilePath (Join-Path $PackageDir "ecosystem.config.js") -Encoding UTF8
    Write-Success "PM2配置文件已创建"
}

# 创建远程部署脚本
function New-RemoteDeployScript {
    param([string]$PackageDir)
    
    Write-Info "创建远程部署脚本..."
    
    $remoteScriptContent = @'
#!/bin/bash

# 远程服务器部署脚本
set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# 检查Node.js
check_nodejs() {
    log_info "检查Node.js环境..."
    
    if ! command -v node &> /dev/null; then
        log_error "未找到Node.js，请先安装Node.js"
        exit 1
    fi
    
    NODE_VERSION=$(node --version)
    log_success "Node.js版本: $NODE_VERSION"
}

# 检查PM2
check_pm2() {
    log_info "检查PM2..."
    
    if ! command -v pm2 &> /dev/null; then
        log_warning "未找到PM2，正在安装..."
        npm install -g pm2
    fi
    
    PM2_VERSION=$(pm2 --version)
    log_success "PM2版本: $PM2_VERSION"
}

# 检查unzip
check_unzip() {
    log_info "检查unzip..."
    
    if ! command -v unzip &> /dev/null; then
        log_warning "未找到unzip，正在安装..."
        apt-get update && apt-get install -y unzip
    fi
    
    log_success "unzip检查完成"
}

# 停止现有服务
stop_service() {
    log_info "停止现有服务..."
    
    if pm2 list | grep -q "hbox-firmware-server"; then
        pm2 stop hbox-firmware-server
        pm2 delete hbox-firmware-server
        log_success "现有服务已停止"
    else
        log_info "未找到运行中的服务"
    fi
}

# 安装依赖
install_dependencies() {
    log_info "安装依赖包..."
    
    if [ -f "package-lock.json" ]; then
        npm ci --production
    else
        npm install --production
    fi
    
    log_success "依赖安装完成"
}

# 创建必要目录
create_directories() {
    log_info "创建必要目录..."
    
    mkdir -p logs
    mkdir -p uploads
    mkdir -p data
    
    log_success "目录创建完成"
}

# 设置权限
set_permissions() {
    log_info "设置文件权限..."
    
    chmod +x start.js
    chmod +x stop.js
    chmod +x deploy-remote.sh
    
    log_success "权限设置完成"
}

# 启动服务
start_service() {
    log_info "启动服务..."
    
    pm2 start ecosystem.config.js --env production
    
    # 保存PM2配置
    pm2 save
    
    # 设置开机自启
    pm2 startup
    
    log_success "服务启动完成"
}

# 显示服务状态
show_status() {
    log_info "服务状态:"
    pm2 list
    pm2 logs hbox-firmware-server --lines 10
}

# 主函数
main() {
    log_info "开始部署 STM32 HBox 固件服务器..."
    
    check_nodejs
    check_pm2
    check_unzip
    stop_service
    install_dependencies
    create_directories
    set_permissions
    start_service
    show_status
    
    log_success "部署完成！"
}

main "$@"
'@
    
    # 使用Unix格式的换行符写入脚本文件
    $remoteScriptContent = $remoteScriptContent -replace "`r`n", "`n"
    [System.IO.File]::WriteAllText((Join-Path $PackageDir "deploy-remote.sh"), $remoteScriptContent, [System.Text.Encoding]::UTF8)
    Write-Success "远程部署脚本已创建"
}

# 传输文件到远程服务器
function Send-FilesToServer {
    Write-Info "传输文件到远程服务器..."
    
    # 检查SSH连接
    $sshTest = if ($UseWsl) {
        wsl ssh -o ConnectTimeout=10 -o BatchMode=yes -i $SshKey -o StrictHostKeyChecking=no "$User@$ServerHost" "exit" 2>$null
    } else {
        ssh -o ConnectTimeout=10 -o BatchMode=yes -i $SshKey -o StrictHostKeyChecking=no "$User@$ServerHost" "exit" 2>$null
    }
    
    if ($LASTEXITCODE -ne 0) {
        Write-Error "无法连接到远程服务器，请检查SSH配置"
        exit 1
    }
    
    # 创建远程目录
    if ($UseWsl) {
        wsl ssh -i $SshKey -o StrictHostKeyChecking=no "$User@$ServerHost" "mkdir -p $DeployPath"
    } else {
        ssh -i $SshKey -o StrictHostKeyChecking=no "$User@$ServerHost" "mkdir -p $DeployPath"
    }
    
    # 传输文件
    try {
        if ($UseWsl) {
            wsl scp -i $SshKey -o StrictHostKeyChecking=no $PackagePath "$User@$ServerHost`:$DeployPath/"
        } else {
            scp -i $SshKey -o StrictHostKeyChecking=no $PackagePath "$User@$ServerHost`:$DeployPath/"
        }
        
        if ($LASTEXITCODE -ne 0) {
            throw "文件传输失败，退出代码: $LASTEXITCODE"
        }
        
        Write-Success "文件传输完成"
    }
    catch {
        Write-Error "文件传输错误: $($_.Exception.Message)"
        throw
    }
}

# 远程部署
function Start-RemoteDeploy {
    Write-Info "开始远程部署..."
    
    # 解压文件
    try {
        $unzipCmd = "cd $DeployPath && unzip -o $PackageName && rm $PackageName"
        if ($UseWsl) {
            wsl ssh -i $SshKey -o StrictHostKeyChecking=no "$User@$ServerHost" $unzipCmd
        } else {
            ssh -i $SshKey -o StrictHostKeyChecking=no "$User@$ServerHost" $unzipCmd
        }
        
        if ($LASTEXITCODE -ne 0) {
            throw "解压文件失败，退出代码: $LASTEXITCODE"
        }
        
        Write-Success "文件解压完成"
    }
    catch {
        Write-Error "解压文件错误: $($_.Exception.Message)"
        throw
    }
    
    # 执行远程部署脚本
    try {
        $deployCmd = "cd $DeployPath && chmod +x deploy-remote.sh && ./deploy-remote.sh"
        if ($UseWsl) {
            wsl ssh -i $SshKey -o StrictHostKeyChecking=no "$User@$ServerHost" $deployCmd
        } else {
            ssh -i $SshKey -o StrictHostKeyChecking=no "$User@$ServerHost" $deployCmd
        }
        
        if ($LASTEXITCODE -ne 0) {
            throw "远程部署脚本执行失败，退出代码: $LASTEXITCODE"
        }
        
        Write-Success "远程部署完成"
    }
    catch {
        Write-Error "远程部署错误: $($_.Exception.Message)"
        throw
    }
}

# 清理临时文件
function Remove-TempFiles {
    Write-Info "清理临时文件..."
    if ($TempDir -and (Test-Path $TempDir)) {
        try {
            # 先尝试删除文件，再删除目录
            Get-ChildItem -Path $TempDir -Recurse -Force | Remove-Item -Force -ErrorAction SilentlyContinue
            Remove-Item -Path $TempDir -Force -ErrorAction SilentlyContinue
            Write-Success "清理完成"
        }
        catch {
            Write-Warning "清理临时文件失败: $($_.Exception.Message)"
            Write-Info "临时目录位置: $TempDir"
            Write-Info "您可以稍后手动删除此目录"
        }
    }
}

# 显示部署结果
function Show-Result {
    Write-Success "部署完成！"
    Write-Host ""
    Write-Host "服务器信息:" -ForegroundColor $White
    Write-Host "  地址: $User@$ServerHost" -ForegroundColor $White
    Write-Host "  路径: $DeployPath/hbox-server" -ForegroundColor $White
    Write-Host "  端口: $Port" -ForegroundColor $White
    Write-Host ""
    Write-Host "管理命令:" -ForegroundColor $White
    Write-Host "  # 查看服务状态" -ForegroundColor $White
    if ($UseWsl) {
        Write-Host "  wsl ssh $User@$ServerHost 'pm2 list'" -ForegroundColor $White
    } else {
        Write-Host "  ssh $User@$ServerHost 'pm2 list'" -ForegroundColor $White
    }
    Write-Host ""
    Write-Host "  # 查看日志" -ForegroundColor $White
    if ($UseWsl) {
        Write-Host "  wsl ssh $User@$ServerHost 'pm2 logs hbox-firmware-server'" -ForegroundColor $White
    } else {
        Write-Host "  ssh $User@$ServerHost 'pm2 logs hbox-firmware-server'" -ForegroundColor $White
    }
    Write-Host ""
    Write-Host "  # 重启服务" -ForegroundColor $White
    if ($UseWsl) {
        Write-Host "  wsl ssh $User@$ServerHost 'pm2 restart hbox-firmware-server'" -ForegroundColor $White
    } else {
        Write-Host "  ssh $User@$ServerHost 'pm2 restart hbox-firmware-server'" -ForegroundColor $White
    }
}

# 主函数
function Main {
    # 脚本目录和配置文件
    $script:ScriptDir = $PSScriptRoot
    $script:ConfigFile = Join-Path $ScriptDir "deploy-config.json"
    $script:UseWsl = $false
    $script:TempDir = $null
    
    # 检查配置文件是否存在
    if (-not (Test-Path $ConfigFile)) {
        Write-Error "配置文件不存在: $ConfigFile"
        Write-Info "请确保 deploy-config.json 文件存在于 tools 目录中"
        exit 1
    }
    
    Write-Info "STM32 HBox 固件服务器部署脚本 (PowerShell版本)"
    Write-Host ""
    
    try {
        Test-Parameters
        Test-Requirements
        New-TempDirectory
        
        New-ServerPackage
        Send-FilesToServer
        Start-RemoteDeploy
        Show-Result
    }
    catch {
        Write-Error "部署失败: $($_.Exception.Message)"
        Write-Error "错误详情: $($_.Exception)"
        Write-Error "堆栈跟踪: $($_.ScriptStackTrace)"
        exit 1
    }
    finally {
        Write-Host ""
        if ($TempDir) {
            Remove-TempFiles
        }
    }
}

# 执行主函数
Main 