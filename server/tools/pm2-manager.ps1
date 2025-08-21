# STM32 HBox 固件服务器PM2管理脚本
# 用于管理PM2进程和查看日志

param(
    [string]$Action = "status",
    [string]$Environment = "prod",
    [int]$Lines = 50
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

$sshCmd = "ssh -i `"$($EnvConfig.ssh_key)`" -o StrictHostKeyChecking=no $($EnvConfig.user)@$($EnvConfig.host)"

Write-Host "========================================" -ForegroundColor Blue
Write-Host "    STM32 HBox 固件服务器PM2管理" -ForegroundColor White
Write-Host "========================================" -ForegroundColor Blue
Write-Host "目标: $($EnvConfig.host):$($EnvConfig.port)" -ForegroundColor White
Write-Host "操作: $Action" -ForegroundColor White
Write-Host ""

switch ($Action.ToLower()) {
    "status" {
        Write-Info "查看PM2状态..."
        $status = Invoke-Expression "$sshCmd 'cd $($EnvConfig.path) && pm2 status'"
        Write-Host $status -ForegroundColor White
    }
    
    "logs" {
        Write-Info "查看应用日志（最后 $Lines 行）..."
        $logs = Invoke-Expression "$sshCmd 'cd $($EnvConfig.path) && pm2 logs hbox-firmware-server --lines $Lines'"
        Write-Host $logs -ForegroundColor Gray
    }
    
    "logs-all" {
        Write-Info "查看所有日志（不限制行数）..."
        $logs = Invoke-Expression "$sshCmd 'cd $($EnvConfig.path) && pm2 logs hbox-firmware-server --lines 0'"
        Write-Host $logs -ForegroundColor Gray
    }
    
    "logs-err" {
        Write-Info "查看错误日志（最后 $Lines 行）..."
        $logs = Invoke-Expression "$sshCmd 'cd $($EnvConfig.path) && pm2 logs hbox-firmware-server --err --lines $Lines'"
        Write-Host $logs -ForegroundColor Red
    }
    
    "logs-out" {
        Write-Info "查看标准输出日志（最后 $Lines 行）..."
        $logs = Invoke-Expression "$sshCmd 'cd $($EnvConfig.path) && pm2 logs hbox-firmware-server --out --lines $Lines'"
        Write-Host $logs -ForegroundColor Green
    }
    
    "restart" {
        Write-Info "重启应用..."
        Invoke-Expression "$sshCmd 'cd $($EnvConfig.path) && pm2 restart hbox-firmware-server'"
        Start-Sleep -Seconds 3
        $status = Invoke-Expression "$sshCmd 'cd $($EnvConfig.path) && pm2 status hbox-firmware-server'"
        Write-Host $status -ForegroundColor White
    }
    
    "stop" {
        Write-Info "停止应用..."
        Invoke-Expression "$sshCmd 'cd $($EnvConfig.path) && pm2 stop hbox-firmware-server'"
        $status = Invoke-Expression "$sshCmd 'cd $($EnvConfig.path) && pm2 status hbox-firmware-server'"
        Write-Host $status -ForegroundColor White
    }
    
    "start" {
        Write-Info "启动应用..."
        Invoke-Expression "$sshCmd 'cd $($EnvConfig.path) && pm2 start hbox-firmware-server'"
        Start-Sleep -Seconds 3
        $status = Invoke-Expression "$sshCmd 'cd $($EnvConfig.path) && pm2 status hbox-firmware-server'"
        Write-Host $status -ForegroundColor White
    }
    
    "delete" {
        Write-Info "删除应用..."
        Invoke-Expression "$sshCmd 'cd $($EnvConfig.path) && pm2 delete hbox-firmware-server'"
        $status = Invoke-Expression "$sshCmd 'cd $($EnvConfig.path) && pm2 status'"
        Write-Host $status -ForegroundColor White
    }
    
    "reload" {
        Write-Info "重新加载应用（零停机重启）..."
        Invoke-Expression "$sshCmd 'cd $($EnvConfig.path) && pm2 reload hbox-firmware-server'"
        Start-Sleep -Seconds 3
        $status = Invoke-Expression "$sshCmd 'cd $($EnvConfig.path) && pm2 status hbox-firmware-server'"
        Write-Host $status -ForegroundColor White
    }
    
    "monit" {
        Write-Info "打开PM2监控界面..."
        Write-Host "请在服务器上运行: pm2 monit" -ForegroundColor Yellow
        Write-Host "或者使用: pm2 plus" -ForegroundColor Yellow
    }
    
    "files" {
        Write-Info "查看日志文件..."
        $files = Invoke-Expression "$sshCmd 'cd $($EnvConfig.path) && ls -la logs/'"
        Write-Host $files -ForegroundColor White
    }
    
    "clean" {
        Write-Info "清理日志文件..."
        Invoke-Expression "$sshCmd 'cd $($EnvConfig.path) && pm2 flush'"
        Write-Success "日志文件已清理"
    }
    
    "health" {
        Write-Info "健康检查..."
        try {
            $response = Invoke-WebRequest -Uri "http://$($EnvConfig.host):$($EnvConfig.port)/health" -UseBasicParsing -TimeoutSec 10
            if ($response.StatusCode -eq 200) {
                Write-Success "应用健康检查通过"
                $healthData = $response.Content | ConvertFrom-Json
                Write-Host "状态: $($healthData.status)" -ForegroundColor Green
                Write-Host "消息: $($healthData.message)" -ForegroundColor Green
                Write-Host "时间: $($healthData.timestamp)" -ForegroundColor Green
            } else {
                Write-Warning "应用响应异常: $($response.StatusCode)"
            }
        }
        catch {
            Write-Error "健康检查失败: $($_.Exception.Message)"
        }
    }
    
    default {
        Write-Host "使用方法:" -ForegroundColor White
        Write-Host "  .\pm2-manager.ps1 [操作] [环境] [行数]" -ForegroundColor White
        Write-Host ""
        Write-Host "可用操作:" -ForegroundColor White
        Write-Host "  status     - 查看PM2状态" -ForegroundColor White
        Write-Host "  logs       - 查看应用日志（默认50行）" -ForegroundColor White
        Write-Host "  logs-all   - 查看所有日志（不限制行数）" -ForegroundColor White
        Write-Host "  logs-err   - 查看错误日志" -ForegroundColor White
        Write-Host "  logs-out   - 查看标准输出日志" -ForegroundColor White
        Write-Host "  restart    - 重启应用" -ForegroundColor White
        Write-Host "  stop       - 停止应用" -ForegroundColor White
        Write-Host "  start      - 启动应用" -ForegroundColor White
        Write-Host "  delete     - 删除应用" -ForegroundColor White
        Write-Host "  reload     - 零停机重启" -ForegroundColor White
        Write-Host "  monit      - 打开监控界面" -ForegroundColor White
        Write-Host "  files      - 查看日志文件" -ForegroundColor White
        Write-Host "  clean      - 清理日志文件" -ForegroundColor White
        Write-Host "  health     - 健康检查" -ForegroundColor White
        Write-Host ""
        Write-Host "示例:" -ForegroundColor White
        Write-Host "  .\pm2-manager.ps1 logs prod 100" -ForegroundColor White
        Write-Host "  .\pm2-manager.ps1 logs-all prod" -ForegroundColor White
        Write-Host "  .\pm2-manager.ps1 restart prod" -ForegroundColor White
    }
}

Write-Host ""
Read-Host "按回车键退出..." 