# STM32 HBox 固件服务器快速部署脚本 (PowerShell版本)
# 提供交互式环境选择

param(
    [switch]$SkipMenu = $false
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

# 显示标题
function Show-Title {
    Clear-Host
    Write-Host "========================================" -ForegroundColor $Blue
    Write-Host "    STM32 HBox 固件服务器快速部署" -ForegroundColor $White
    Write-Host "========================================" -ForegroundColor $Blue
    Write-Host ""
}

# 检查环境
function Test-Environment {
    Write-Info "检查部署环境..."
    
    # 检查PowerShell版本
    if ($PSVersionTable.PSVersion.Major -lt 5) {
        Write-Error "需要PowerShell 5.0或更高版本"
        return $false
    }
    
    # 检查脚本文件
    if (-not (Test-Path (Join-Path $ScriptDir "deploy.ps1"))) {
        Write-Error "未找到deploy.ps1脚本文件"
        return $false
    }
    
    # 检查配置文件
    if (-not (Test-Path $ConfigFile)) {
        Write-Error "未找到deploy-config.json配置文件"
        Write-Info "请先运行 setup.ps1 创建配置文件"
        return $false
    }
    
    Write-Success "环境检查通过"
    return $true
}

# 显示主菜单
function Show-MainMenu {
    Show-Title
    
    Write-Host "请选择操作:" -ForegroundColor $White
    Write-Host ""
    Write-Host "1. 部署到开发环境 (dev)" -ForegroundColor $Yellow
    Write-Host "2. 部署到测试环境 (test)" -ForegroundColor $Yellow
    Write-Host "3. 部署到生产环境 (prod)" -ForegroundColor $Yellow
    Write-Host "4. 测试连接" -ForegroundColor $Yellow
    Write-Host "5. 服务管理" -ForegroundColor $Yellow
    Write-Host "6. 退出" -ForegroundColor $Yellow
    Write-Host ""
}

# 显示测试连接菜单
function Show-TestMenu {
    Show-Title
    
    Write-Host "请选择要测试的环境:" -ForegroundColor $White
    Write-Host ""
    Write-Host "1. 开发环境 (dev)" -ForegroundColor $Yellow
    Write-Host "2. 测试环境 (test)" -ForegroundColor $Yellow
    Write-Host "3. 生产环境 (prod)" -ForegroundColor $Yellow
    Write-Host "4. 返回主菜单" -ForegroundColor $Yellow
    Write-Host ""
}

# 显示服务管理菜单
function Show-ManagerMenu {
    Show-Title
    
    Write-Host "请选择环境:" -ForegroundColor $White
    Write-Host ""
    Write-Host "1. 开发环境 (dev)" -ForegroundColor $Yellow
    Write-Host "2. 测试环境 (test)" -ForegroundColor $Yellow
    Write-Host "3. 生产环境 (prod)" -ForegroundColor $Yellow
    Write-Host "4. 返回主菜单" -ForegroundColor $Yellow
    Write-Host ""
}

# 显示服务操作菜单
function Show-ServiceMenu {
    param([string]$Environment)
    
    Show-Title
    Write-Host "环境: $Environment" -ForegroundColor $Green
    Write-Host ""
    Write-Host "请选择操作:" -ForegroundColor $White
    Write-Host ""
    Write-Host "1. 查看服务状态" -ForegroundColor $Yellow
    Write-Host "2. 启动服务" -ForegroundColor $Yellow
    Write-Host "3. 停止服务" -ForegroundColor $Yellow
    Write-Host "4. 重启服务" -ForegroundColor $Yellow
    Write-Host "5. 查看日志" -ForegroundColor $Yellow
    Write-Host "6. 健康检查" -ForegroundColor $Yellow
    Write-Host "7. 备份数据" -ForegroundColor $Yellow
    Write-Host "8. 监控服务" -ForegroundColor $Yellow
    Write-Host "9. 返回环境选择" -ForegroundColor $Yellow
    Write-Host "10. 返回主菜单" -ForegroundColor $Yellow
    Write-Host ""
}

# 执行部署
function Invoke-Deploy {
    param([string]$Environment)
    
    Write-Info "部署到 $Environment 环境..."
    
    try {
        $deployScript = Join-Path $ScriptDir "deploy.ps1"
        & $deployScript $Environment
        
        if ($LASTEXITCODE -eq 0) {
            Write-Success "部署完成！"
        } else {
            Write-Error "部署失败！"
        }
    }
    catch {
        Write-Error "部署过程中发生错误: $($_.Exception.Message)"
    }
    
    Read-Host "按回车键继续..."
}

# 执行连接测试
function Invoke-TestConnection {
    param([string]$Environment)
    
    Write-Info "测试 $Environment 环境连接..."
    
    try {
        $testScript = Join-Path $ScriptDir "test-connection.ps1"
        & $testScript $Environment
        
        if ($LASTEXITCODE -eq 0) {
            Write-Success "连接测试完成！"
        } else {
            Write-Error "连接测试失败！"
        }
    }
    catch {
        Write-Error "连接测试过程中发生错误: $($_.Exception.Message)"
    }
    
    Read-Host "按回车键继续..."
}

# 执行服务管理
function Invoke-ServiceManager {
    param([string]$Environment, [string]$Command)
    
    Write-Info "执行服务管理命令: $Command"
    
    try {
        $managerScript = Join-Path $ScriptDir "service-manager.ps1"
        & $managerScript $Environment $Command
        
        if ($LASTEXITCODE -eq 0) {
            Write-Success "服务管理命令执行完成！"
        } else {
            Write-Error "服务管理命令执行失败！"
        }
    }
    catch {
        Write-Error "服务管理过程中发生错误: $($_.Exception.Message)"
    }
    
    Read-Host "按回车键继续..."
}

# 处理主菜单选择
function Handle-MainMenu {
    param([string]$Choice)
    
    switch ($Choice) {
        "1" {
            Invoke-Deploy "dev"
        }
        "2" {
            Invoke-Deploy "test"
        }
        "3" {
            Invoke-Deploy "prod"
        }
        "4" {
            Handle-TestMenu
        }
        "5" {
            Handle-ManagerMenu
        }
        "6" {
            Write-Info "退出部署工具"
            exit 0
        }
        default {
            Write-Error "无效选择，请输入 1-6"
            Read-Host "按回车键继续..."
        }
    }
}

# 处理测试菜单选择
function Handle-TestMenu {
    Show-TestMenu
    $choice = Read-Host "请输入选择 (1-4)"
    
    switch ($choice) {
        "1" {
            Invoke-TestConnection "dev"
        }
        "2" {
            Invoke-TestConnection "test"
        }
        "3" {
            Invoke-TestConnection "prod"
        }
        "4" {
            return
        }
        default {
            Write-Error "无效选择，请输入 1-4"
            Read-Host "按回车键继续..."
            Handle-TestMenu
        }
    }
}

# 处理服务管理菜单选择
function Handle-ManagerMenu {
    Show-ManagerMenu
    $envChoice = Read-Host "请输入环境选择 (1-4)"
    
    $environment = switch ($envChoice) {
        "1" { "dev" }
        "2" { "test" }
        "3" { "prod" }
        "4" { return }
        default {
            Write-Error "无效选择，请输入 1-4"
            Read-Host "按回车键继续..."
            Handle-ManagerMenu
            return
        }
    }
    
    Handle-ServiceMenu $environment
}

# 处理服务操作菜单选择
function Handle-ServiceMenu {
    param([string]$Environment)
    
    Show-ServiceMenu $Environment
    $cmdChoice = Read-Host "请输入操作选择 (1-10)"
    
    $command = switch ($cmdChoice) {
        "1" { "status" }
        "2" { "start" }
        "3" { "stop" }
        "4" { "restart" }
        "5" { "logs" }
        "6" { "health" }
        "7" { "backup" }
        "8" { "monitor" }
        "9" { 
            Handle-ManagerMenu
            return
        }
        "10" { 
            return
        }
        default {
            Write-Error "无效选择，请输入 1-10"
            Read-Host "按回车键继续..."
            Handle-ServiceMenu $Environment
            return
        }
    }
    
    if ($command) {
        Invoke-ServiceManager $Environment $command
    }
    
    Handle-ServiceMenu $Environment
}

# 主函数
function Main {
    # 检查环境
    if (-not (Test-Environment)) {
        Write-Error "环境检查失败，请解决上述问题后重试"
        Read-Host "按回车键退出..."
        exit 1
    }
    
    # 主循环
    while ($true) {
        Show-MainMenu
        $choice = Read-Host "请输入选择 (1-6)"
        Handle-MainMenu $choice
    }
}

# 执行主函数
Main 