# STM32 HBox 固件服务器简化部署脚本
# 直接部署到生产环境，无需选择

Write-Host "========================================" -ForegroundColor Blue
Write-Host "    STM32 HBox 固件服务器部署" -ForegroundColor White
Write-Host "========================================" -ForegroundColor Blue
Write-Host ""

# 检查文件
$requiredFiles = @("deploy.ps1", "deploy-config.json")
foreach ($file in $requiredFiles) {
    if (-not (Test-Path $file)) {
        Write-Host "[ERROR] 缺少文件: $file" -ForegroundColor Red
        Read-Host "按回车键退出..."
        exit 1
    }
}

Write-Host "[INFO] 开始部署到生产环境..." -ForegroundColor Blue
Write-Host ""

# 直接调用部署脚本
try {
    & .\deploy.ps1 prod
    if ($LASTEXITCODE -eq 0) {
        Write-Host ""
        Write-Host "[SUCCESS] 部署完成！" -ForegroundColor Green
    } else {
        Write-Host ""
        Write-Host "[ERROR] 部署失败！" -ForegroundColor Red
    }
}
catch {
    Write-Host ""
    Write-Host "[ERROR] 部署过程中发生错误: $($_.Exception.Message)" -ForegroundColor Red
}

Write-Host ""
Read-Host "按回车键退出..." 