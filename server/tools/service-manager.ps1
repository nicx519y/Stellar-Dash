# STM32 HBox 固件服务器服务管理脚本 (PowerShell版本)
# 使用方法: .\service-manager.ps1 [环境名] [命令]

param(
    [string]$Environment = "",
    [string]$Command = ""
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

# 脚本目录和配置文件
$ScriptDir = $PSScriptRoot
$ConfigFile = Join-Path $ScriptDir "deploy-config.json"
$UseWsl = $false

# 显示帮助信息
function Show-Help {
    Write-Host "STM32 HBox 固件服务器服务管理脚本 (PowerShell版本)" -ForegroundColor $White
    Write-Host ""
    Write-Host "使用方法:" -ForegroundColor $White
    Write-Host "  .\service-manager.ps1 [环境名] [命令]" -ForegroundColor $White
    Write-Host ""
    Write-Host "命令:" -ForegroundColor $White
    Write-Host "  status    - 查看服务状态" -ForegroundColor $White
    Write-Host "  start     - 启动服务" -ForegroundColor $White
    Write-Host "  stop      - 停止服务" -ForegroundColor $White
    Write-Host "  restart   - 重启服务" -ForegroundColor $White
    Write-Host "  reload    - 重载服务" -ForegroundColor $White
    Write-Host "  logs      - 查看日志" -ForegroundColor $White
    Write-Host "  monitor   - 监控服务" -ForegroundColor $White
    Write-Host "  backup    - 备份数据" -ForegroundColor $White
    Write-Host "  health    - 健康检查" -ForegroundColor $White
    Write-Host ""
    Write-Host "环境名:" -ForegroundColor $White
    if (Test-Path $ConfigFile) {
        try {
            $config = Get-Content $ConfigFile | ConvertFrom-Json
            $config.environments.PSObject.Properties | ForEach-Object {
                $envName = $_.Name
                $envDesc = $_.Value.description
                Write-Host "  $envName - $envDesc" -ForegroundColor $White
            }
        }
        catch {
            Write-Host "  dev  - 开发环境" -ForegroundColor $White
            Write-Host "  test - 测试环境" -ForegroundColor $White
            Write-Host "  prod - 生产环境" -ForegroundColor $White
        }
    }
    else {
        Write-Host "  dev  - 开发环境" -ForegroundColor $White
        Write-Host "  test - 测试环境" -ForegroundColor $White
        Write-Host "  prod - 生产环境" -ForegroundColor $White
    }
    Write-Host ""
    Write-Host "示例:" -ForegroundColor $White
    Write-Host "  .\service-manager.ps1 prod status" -ForegroundColor $White
    Write-Host "  .\service-manager.ps1 dev logs" -ForegroundColor $White
    Write-Host "  .\service-manager.ps1 test restart" -ForegroundColor $White
    Write-Host ""
    Write-Host "配置文件: $ConfigFile" -ForegroundColor $White
}

# 检查SSH工具
function Test-SshTool {
    $sshPath = Get-Command "ssh" -ErrorAction SilentlyContinue
    if (-not $sshPath) {
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
}

# 加载配置
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
        
    }
    catch {
        Write-Error "加载配置文件失败: $($_.Exception.Message)"
        exit 1
    }
}

# 执行远程命令
function Invoke-RemoteCommand {
    param([string]$Command)
    
    if ($UseWsl) {
        wsl ssh -i $SshKey -o StrictHostKeyChecking=no "$User@$ServerHost" $Command
    } else {
        ssh -i $SshKey -o StrictHostKeyChecking=no "$User@$ServerHost" $Command
    }
}

# 查看服务状态
function Get-ServiceStatus {
    Write-Info "查看服务状态..."
    Invoke-RemoteCommand "cd $DeployPath/hbox-server && pm2 list"
}

# 启动服务
function Start-Service {
    Write-Info "启动服务..."
    Invoke-RemoteCommand "cd $DeployPath/hbox-server && pm2 start ecosystem.config.js --env production"
    Write-Success "服务启动完成"
}

# 停止服务
function Stop-Service {
    Write-Info "停止服务..."
    Invoke-RemoteCommand "cd $DeployPath/hbox-server && pm2 stop hbox-firmware-server"
    Write-Success "服务停止完成"
}

# 重启服务
function Restart-Service {
    Write-Info "重启服务..."
    Invoke-RemoteCommand "cd $DeployPath/hbox-server && pm2 restart hbox-firmware-server"
    Write-Success "服务重启完成"
}

# 重载服务
function Reload-Service {
    Write-Info "重载服务..."
    Invoke-RemoteCommand "cd $DeployPath/hbox-server && pm2 reload hbox-firmware-server"
    Write-Success "服务重载完成"
}

# 查看日志
function Get-ServiceLogs {
    Write-Info "查看服务日志..."
    Invoke-RemoteCommand "cd $DeployPath/hbox-server && pm2 logs hbox-firmware-server --lines 50"
}

# 监控服务
function Monitor-Service {
    Write-Info "监控服务状态..."
    Invoke-RemoteCommand "cd $DeployPath/hbox-server && pm2 monit"
}

# 备份数据
function Backup-Data {
    Write-Info "备份数据..."
    $backupDir = "$DeployPath/backups"
    $backupName = "hbox-backup-$(Get-Date -Format 'yyyyMMdd-HHmmss').tar.gz"
    
    Invoke-RemoteCommand "mkdir -p $backupDir"
    Invoke-RemoteCommand "cd $DeployPath/hbox-server && tar -czf $backupDir/$backupName data/ uploads/ logs/"
    
    Write-Success "数据备份完成: $backupDir/$backupName"
}

# 健康检查
function Test-ServiceHealth {
    Write-Info "执行健康检查..."
    
    # 检查PM2进程
    $pm2Status = Invoke-RemoteCommand "cd $DeployPath/hbox-server && pm2 list | grep hbox-firmware-server"
    
    if ($pm2Status -match "online") {
        Write-Success "PM2进程状态: 正常"
    } else {
        Write-Error "PM2进程状态: 异常"
        Write-Host $pm2Status
        return
    }
    
    # 检查端口监听
    $portStatus = Invoke-RemoteCommand "netstat -tlnp | grep :$Port || ss -tlnp | grep :$Port"
    
    if ($portStatus) {
        Write-Success "端口监听状态: 正常 (端口 $Port)"
    } else {
        Write-Error "端口监听状态: 异常 (端口 $Port)"
        return
    }
    
    # 检查HTTP响应
    $httpStatus = Invoke-RemoteCommand "curl -s -o /dev/null -w '%{http_code}' http://localhost:$Port/health || echo '000'"
    
    if ($httpStatus -eq "200") {
        Write-Success "HTTP健康检查: 正常 (状态码: $httpStatus)"
    } else {
        Write-Warning "HTTP健康检查: 异常 (状态码: $httpStatus)"
    }
    
    # 检查磁盘空间
    $diskUsage = Invoke-RemoteCommand "df -h $DeployPath | tail -1 | awk '{print \$5}' | sed 's/%//'"
    
    if ([int]$diskUsage -lt 80) {
        Write-Success "磁盘空间: 正常 (使用率: ${diskUsage}%)"
    } else {
        Write-Warning "磁盘空间: 警告 (使用率: ${diskUsage}%)"
    }
    
    # 检查内存使用
    $memoryUsage = Invoke-RemoteCommand "free | grep Mem | awk '{printf \"%.1f\", \$3/\$2 * 100.0}'"
    
    if ([double]$memoryUsage -lt 80) {
        Write-Success "内存使用: 正常 (使用率: ${memoryUsage}%)"
    } else {
        Write-Warning "内存使用: 警告 (使用率: ${memoryUsage}%)"
    }
}

# 主函数
function Main {
    # 检查参数
    if ($Environment -eq "-h" -or $Environment -eq "--help") {
        Show-Help
        exit 0
    }
    
    if (-not $Environment -or -not $Command) {
        Write-Error "参数不足"
        Show-Help
        exit 1
    }
    
    Write-Info "STM32 HBox 固件服务器服务管理"
    Write-Host ""
    
    # 检查SSH工具
    Test-SshTool
    
    # 加载配置
    Load-Config $Environment
    
    Write-Info "环境: $Environment ($Description)"
    Write-Info "服务器: $User@$ServerHost"
    Write-Info "命令: $Command"
    Write-Host ""
    
    # 执行命令
    switch ($Command.ToLower()) {
        "status" {
            Get-ServiceStatus
        }
        "start" {
            Start-Service
        }
        "stop" {
            Stop-Service
        }
        "restart" {
            Restart-Service
        }
        "reload" {
            Reload-Service
        }
        "logs" {
            Get-ServiceLogs
        }
        "monitor" {
            Monitor-Service
        }
        "backup" {
            Backup-Data
        }
        "health" {
            Test-ServiceHealth
        }
        default {
            Write-Error "未知命令: $Command"
            Show-Help
            exit 1
        }
    }
}

# 执行主函数
Main 