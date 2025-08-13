# STM32 HBox 固件服务器部署工具启动脚本

Write-Host "========================================" -ForegroundColor Blue
Write-Host "    STM32 HBox 固件服务器部署工具" -ForegroundColor White
Write-Host "========================================" -ForegroundColor Blue
Write-Host ""

# 检查当前目录
$currentDir = Get-Location
Write-Host "当前目录: $currentDir" -ForegroundColor Yellow

# 检查必需文件
$requiredFiles = @(
    "deploy.ps1",
    "test-connection.ps1",
    "service-manager.ps1", 
    "quick-deploy.ps1",
    "deploy-config.json"
)

Write-Host "检查必需文件..." -ForegroundColor Blue
$missingFiles = @()

foreach ($file in $requiredFiles) {
    if (Test-Path $file) {
        Write-Host "✓ $file" -ForegroundColor Green
    } else {
        Write-Host "✗ $file" -ForegroundColor Red
        $missingFiles += $file
    }
}

Write-Host ""

if ($missingFiles.Count -gt 0) {
    Write-Host "缺少以下文件:" -ForegroundColor Red
    foreach ($file in $missingFiles) {
        Write-Host "  - $file" -ForegroundColor Red
    }
    Write-Host ""
    Write-Host "请确保所有文件都在当前目录中。" -ForegroundColor Yellow
    Read-Host "按回车键退出..."
    exit 1
}

Write-Host "所有文件检查通过！" -ForegroundColor Green
Write-Host ""

# 显示可用选项
Write-Host "可用命令:" -ForegroundColor White
Write-Host "1. .\quick-deploy.ps1     - 交互式部署工具 (推荐)" -ForegroundColor Yellow
Write-Host "2. .\deploy.ps1 prod      - 直接部署到生产环境" -ForegroundColor Yellow
Write-Host "3. .\test-connection.ps1 prod - 测试生产环境连接" -ForegroundColor Yellow
Write-Host "4. .\service-manager.ps1 prod status - 查看服务状态" -ForegroundColor Yellow
Write-Host "5. .\check-files.ps1      - 检查文件完整性" -ForegroundColor Yellow
Write-Host ""

# 询问用户选择
$choice = Read-Host "请选择操作 (1-5，或按回车启动交互式工具)"

switch ($choice) {
    "1" { 
        Write-Host "启动交互式部署工具..." -ForegroundColor Blue
        .\quick-deploy.ps1 
    }
    "2" { 
        Write-Host "部署到生产环境..." -ForegroundColor Blue
        .\deploy.ps1 prod 
    }
    "3" { 
        Write-Host "测试生产环境连接..." -ForegroundColor Blue
        .\test-connection.ps1 prod 
    }
    "4" { 
        Write-Host "查看服务状态..." -ForegroundColor Blue
        .\service-manager.ps1 prod status 
    }
    "5" { 
        Write-Host "检查文件完整性..." -ForegroundColor Blue
        .\check-files.ps1 
    }
    default { 
        Write-Host "启动交互式部署工具..." -ForegroundColor Blue
        .\quick-deploy.ps1 
    }
} 