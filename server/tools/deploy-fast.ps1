# STM32 HBox 固件服务器超快速部署脚本
# 只上传核心文件并热部署，不包含Nginx配置

param(
    [string]$Environment = "prod"
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

# 读取配置文件
$ConfigFile = "deploy-config.json"
if (-not (Test-Path $ConfigFile)) {
    Write-Error "配置文件不存在: $ConfigFile"
    exit 1
}

$Config = Get-Content $ConfigFile | ConvertFrom-Json
$EnvConfig = $Config.environments.$Environment

if (-not $EnvConfig) {
    Write-Error "环境配置不存在: $Environment"
    exit 1
}

Write-Host "========================================" -ForegroundColor Blue
Write-Host "    STM32 HBox 固件服务器超快速部署" -ForegroundColor White
Write-Host "========================================" -ForegroundColor Blue
Write-Host "目标: $($EnvConfig.host):$($EnvConfig.port)" -ForegroundColor White
Write-Host "域名: $($EnvConfig.domain)" -ForegroundColor White
Write-Host ""

# 1. 创建临时打包目录
Write-Info "步骤 1: 打包核心文件..."
$TempDir = Join-Path $env:TEMP "hbox-deploy-$(Get-Date -Format 'yyyyMMdd-HHmmss')"
New-Item -ItemType Directory -Path $TempDir -Force | Out-Null

# 切换到服务器目录
$serverDir = Split-Path $PSScriptRoot
Set-Location $serverDir

# 创建打包目录
$packageDir = Join-Path $TempDir "hbox-server"
New-Item -ItemType Directory -Path $packageDir -Force | Out-Null

# 只复制核心文件
Write-Info "复制核心文件..."
Copy-Item -Path "src" -Destination $packageDir -Recurse -Force
Copy-Item -Path "package.json" -Destination $packageDir -Force
Copy-Item -Path "package-lock.json" -Destination $packageDir -Force
Copy-Item -Path "start.js" -Destination $packageDir -Force
Copy-Item -Path "stop.js" -Destination $packageDir -Force

# 创建必要的目录
$logsDir = Join-Path $packageDir "logs"
New-Item -ItemType Directory -Path $logsDir -Force | Out-Null

# 打包
Set-Location $TempDir
$PackageName = "hbox-server-$(Get-Date -Format 'yyyyMMdd-HHmmss').zip"
Compress-Archive -Path "hbox-server\*" -DestinationPath $PackageName -Force
$PackagePath = Join-Path $TempDir $PackageName

Write-Success "打包完成: $PackageName"

# 2. 上传到服务器
Write-Info "步骤 2: 上传到服务器..."
$sshCmd = "ssh -i `"$($EnvConfig.ssh_key)`" -o StrictHostKeyChecking=no $($EnvConfig.user)@$($EnvConfig.host)"
$scpCmd = "scp -i `"$($EnvConfig.ssh_key)`" -o StrictHostKeyChecking=no"

# 上传包文件
Invoke-Expression "$scpCmd `"$PackagePath`" $($EnvConfig.user)@$($EnvConfig.host):$($EnvConfig.path)/"

# 3. 在服务器上部署
Write-Info "步骤 3: 服务器端部署..."

# 解压并部署
$deployCommands = @"
cd $($EnvConfig.path)
unzip -o $PackageName
rm -f $PackageName
npm install --production
pm2 restart hbox-firmware-server
"@

$deployCommands | Invoke-Expression "$sshCmd"

# 4. 检查部署状态
Write-Info "步骤 4: 检查部署状态..."
Start-Sleep -Seconds 3

$pm2Status = Invoke-Expression "$sshCmd 'pm2 status hbox-firmware-server'"
if ($pm2Status -match "online") {
    Write-Success "PM2热部署完成"
} else {
    Write-Warning "PM2状态异常，请检查日志"
}

# 5. 快速健康检查
Write-Info "步骤 5: 健康检查..."

# 检查直接端口访问
try {
    $directResponse = Invoke-WebRequest -Uri "http://$($EnvConfig.host):$($EnvConfig.port)/health" -UseBasicParsing -TimeoutSec 5
    if ($directResponse.StatusCode -eq 200) {
        Write-Success "直接端口访问正常"
    } else {
        Write-Warning "直接端口访问异常: $($directResponse.StatusCode)"
    }
}
catch {
    Write-Warning "直接端口访问检查失败"
}

# 检查域名访问
try {
    $domainResponse = Invoke-WebRequest -Uri "https://$($EnvConfig.domain)/health" -UseBasicParsing -TimeoutSec 5
    if ($domainResponse.StatusCode -eq 200) {
        Write-Success "域名访问正常"
    } else {
        Write-Warning "域名访问异常: $($domainResponse.StatusCode)"
    }
}
catch {
    Write-Warning "域名访问检查失败"
}

# 6. 清理临时文件
Write-Info "步骤 6: 清理临时文件..."
Remove-Item -Path $TempDir -Recurse -Force -ErrorAction SilentlyContinue

Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "    超快速部署完成！" -ForegroundColor White
Write-Host "========================================" -ForegroundColor Green
Write-Host "访问方式 1: http://$($EnvConfig.host):$($EnvConfig.port)" -ForegroundColor White
Write-Host "访问方式 2: https://$($EnvConfig.domain)" -ForegroundColor White
Write-Host ""
Write-Host "部署时间: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" -ForegroundColor Gray
Write-Host ""

Read-Host "按回车键退出..." 