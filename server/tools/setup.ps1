# STM32 HBox 固件服务器部署环境设置脚本
# 使用方法: .\setup.ps1

param(
    [switch]$SkipChecks = $false
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

# 检查PowerShell版本
function Test-PowerShellVersion {
    Write-Info "检查PowerShell版本..."
    
    $version = $PSVersionTable.PSVersion
    if ($version.Major -lt 5) {
        Write-Error "需要PowerShell 5.0或更高版本，当前版本: $version"
        Write-Info "请升级PowerShell或安装PowerShell Core"
        return $false
    }
    
    Write-Success "PowerShell版本检查通过: $version"
    return $true
}

# 检查7-Zip
function Test-7Zip {
    Write-Info "检查7-Zip..."
    
    $7zPath = Get-Command "7z" -ErrorAction SilentlyContinue
    if (-not $7zPath) {
        # 尝试查找7-Zip安装路径
        $7zPaths = @(
            "${env:ProgramFiles}\7-Zip\7z.exe",
            "${env:ProgramFiles(x86)}\7-Zip\7z.exe",
            "C:\Program Files\7-Zip\7z.exe",
            "C:\Program Files (x86)\7-Zip\7z.exe"
        )
        
        $7zPath = $7zPaths | Where-Object { Test-Path $_ } | Select-Object -First 1
        
        if (-not $7zPath) {
            Write-Warning "未找到7-Zip，请安装7-Zip工具"
            Write-Info "下载地址: https://www.7-zip.org/"
            Write-Info "安装完成后重新运行此脚本"
            return $false
        }
    }
    
    Write-Success "7-Zip检查通过"
    return $true
}

# 检查SSH工具
function Test-SshTools {
    Write-Info "检查SSH工具..."
    
    $sshPath = Get-Command "ssh" -ErrorAction SilentlyContinue
    if (-not $sshPath) {
        # 尝试WSL
        try {
            & wsl ssh -V 2>$null
            Write-Success "找到WSL SSH"
            return $true
        }
        catch {
            Write-Warning "未找到SSH命令，请安装以下任一工具:"
            Write-Info "1. Windows Subsystem for Linux (WSL) - 推荐"
            Write-Info "2. Git for Windows (包含Git Bash)"
            Write-Info "3. OpenSSH for Windows"
            return $false
        }
    }
    
    Write-Success "SSH工具检查通过"
    return $true
}

# 设置PowerShell执行策略
function Set-ExecutionPolicy {
    Write-Info "设置PowerShell执行策略..."
    
    try {
        $currentPolicy = Get-ExecutionPolicy -Scope CurrentUser
        if ($currentPolicy -eq "Restricted") {
            Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser -Force
            Write-Success "执行策略已设置为RemoteSigned"
        } else {
            Write-Success "执行策略已正确设置: $currentPolicy"
        }
        return $true
    }
    catch {
        Write-Error "设置执行策略失败: $($_.Exception.Message)"
        return $false
    }
}

# 创建示例配置文件
function New-SampleConfig {
    Write-Info "创建示例配置文件..."
    
    $configFile = Join-Path $PSScriptRoot "deploy-config.json"
    
    if (Test-Path $configFile) {
        Write-Warning "配置文件已存在: $configFile"
        $overwrite = Read-Host "是否覆盖? (y/N)"
        if ($overwrite -ne "y" -and $overwrite -ne "Y") {
            Write-Info "跳过配置文件创建"
            return $true
        }
    }
    
    $sampleConfig = @{
        environments = @{
            dev = @{
                host = "192.168.1.100"
                user = "ubuntu"
                path = "/home/ubuntu/hbox-server-dev"
                ssh_key = "~/.ssh/id_rsa"
                port = 3001
                description = "开发环境"
            }
            test = @{
                host = "192.168.1.101"
                user = "ubuntu"
                path = "/home/ubuntu/hbox-server-test"
                ssh_key = "~/.ssh/id_rsa"
                port = 3002
                description = "测试环境"
            }
            prod = @{
                host = "192.168.1.102"
                user = "root"
                path = "/opt/hbox-server"
                ssh_key = "~/.ssh/id_rsa"
                port = 3000
                description = "生产环境"
            }
        }
        default_env = "prod"
        backup_enabled = $true
        backup_retention_days = 7
        health_check_url = "http://localhost:3000/health"
        health_check_timeout = 30
    }
    
    try {
        $sampleConfig | ConvertTo-Json -Depth 10 | Out-File -FilePath $configFile -Encoding UTF8
        Write-Success "示例配置文件已创建: $configFile"
        Write-Info "请根据您的实际环境修改配置文件"
        return $true
    }
    catch {
        Write-Error "创建配置文件失败: $($_.Exception.Message)"
        return $false
    }
}

# 生成SSH密钥
function New-SshKey {
    Write-Info "检查SSH密钥..."
    
    $sshKeyPath = "$env:USERPROFILE\.ssh\id_rsa"
    if (Test-Path $sshKeyPath) {
        Write-Success "SSH密钥已存在: $sshKeyPath"
        return $true
    }
    
    $generate = Read-Host "未找到SSH密钥，是否生成新的SSH密钥? (y/N)"
    if ($generate -ne "y" -and $generate -ne "Y") {
        Write-Info "跳过SSH密钥生成"
        return $true
    }
    
    try {
        # 创建.ssh目录
        $sshDir = "$env:USERPROFILE\.ssh"
        if (-not (Test-Path $sshDir)) {
            New-Item -ItemType Directory -Path $sshDir -Force | Out-Null
        }
        
        # 生成SSH密钥
        $email = Read-Host "请输入您的邮箱地址"
        ssh-keygen -t rsa -b 4096 -C $email -f $sshKeyPath -N '""'
        
        Write-Success "SSH密钥已生成: $sshKeyPath"
        Write-Info "请将公钥复制到目标服务器:"
        Write-Host "type $env:USERPROFILE\.ssh\id_rsa.pub" -ForegroundColor $Yellow
        return $true
    }
    catch {
        Write-Error "生成SSH密钥失败: $($_.Exception.Message)"
        return $false
    }
}

# 显示使用说明
function Show-Usage {
    Write-Host ""
    Write-Host "=== STM32 HBox 固件服务器部署工具设置完成 ===" -ForegroundColor $Green
    Write-Host ""
    Write-Host "下一步操作:" -ForegroundColor $White
    Write-Host "1. 编辑配置文件: deploy-config.json" -ForegroundColor $White
    Write-Host "2. 将SSH公钥复制到目标服务器" -ForegroundColor $White
    Write-Host "3. 测试SSH连接" -ForegroundColor $White
    Write-Host "4. 运行部署脚本" -ForegroundColor $White
    Write-Host ""
    Write-Host "常用命令:" -ForegroundColor $White
    Write-Host "  # 部署到生产环境" -ForegroundColor $Yellow
    Write-Host "  .\deploy.ps1 prod" -ForegroundColor $Yellow
    Write-Host ""
    Write-Host "  # 查看服务状态" -ForegroundColor $Yellow
    Write-Host "  .\service-manager.ps1 prod status" -ForegroundColor $Yellow
    Write-Host ""
    Write-Host "  # 查看帮助" -ForegroundColor $Yellow
    Write-Host "  .\deploy.ps1 --help" -ForegroundColor $Yellow
    Write-Host "  .\service-manager.ps1 --help" -ForegroundColor $Yellow
    Write-Host ""
}

# 主函数
function Main {
    Write-Host "STM32 HBox 固件服务器部署环境设置脚本" -ForegroundColor $White
    Write-Host ""
    
    $allPassed = $true
    
    # 检查PowerShell版本
    if (-not $SkipChecks) {
        if (-not (Test-PowerShellVersion)) {
            $allPassed = $false
        }
    }
    
    # 设置执行策略
    if (-not (Set-ExecutionPolicy)) {
        $allPassed = $false
    }
    
    # 检查7-Zip
    if (-not $SkipChecks) {
        if (-not (Test-7Zip)) {
            $allPassed = $false
        }
    }
    
    # 检查SSH工具
    if (-not $SkipChecks) {
        if (-not (Test-SshTools)) {
            $allPassed = $false
        }
    }
    
    # 生成SSH密钥
    if (-not (New-SshKey)) {
        $allPassed = $false
    }
    
    # 创建示例配置文件
    if (-not (New-SampleConfig)) {
        $allPassed = $false
    }
    
    if ($allPassed) {
        Show-Usage
        Write-Success "环境设置完成！"
    } else {
        Write-Warning "环境设置未完全完成，请根据上述提示安装缺失的工具"
    }
}

# 执行主函数
Main 