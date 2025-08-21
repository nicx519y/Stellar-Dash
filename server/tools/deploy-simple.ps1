# STM32 HBox 固件服务器精简部署脚本
# 只包含文件上传、PM2热部署和Nginx重启

Write-Host "========================================" -ForegroundColor Blue
Write-Host "    STM32 HBox 固件服务器快速部署" -ForegroundColor White
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

# 读取配置
$Config = Get-Content "deploy-config.json" | ConvertFrom-Json
$EnvConfig = $Config.environments.prod

Write-Host "[INFO] 开始快速部署..." -ForegroundColor Blue
Write-Host "目标: $($EnvConfig.host):$($EnvConfig.port)" -ForegroundColor White
Write-Host "域名: $($EnvConfig.domain)" -ForegroundColor White
Write-Host ""

# 1. 快速部署应用（只上传最新文件）
Write-Host "[INFO] 步骤 1: 上传最新文件并部署..." -ForegroundColor Blue
try {
    & .\deploy.ps1 prod
    if ($LASTEXITCODE -eq 0) {
        Write-Host "[SUCCESS] 应用部署完成！" -ForegroundColor Green
    } else {
        Write-Host "[ERROR] 应用部署失败！" -ForegroundColor Red
        Read-Host "按回车键退出..."
        exit 1
    }
}
catch {
    Write-Host "[ERROR] 部署过程中发生错误: $($_.Exception.Message)" -ForegroundColor Red
    Read-Host "按回车键退出..."
    exit 1
}

# 2. PM2热部署
Write-Host "[INFO] 步骤 2: PM2热部署..." -ForegroundColor Blue
$sshCmd = "ssh -i `"$($EnvConfig.ssh_key)`" -o StrictHostKeyChecking=no $($EnvConfig.user)@$($EnvConfig.host)"

# 重启PM2应用（使用配置文件）
Invoke-Expression "$sshCmd 'cd $($EnvConfig.path) && pm2 restart ecosystem.config.js'"
Start-Sleep -Seconds 3

# 检查PM2状态
$pm2Status = Invoke-Expression "$sshCmd 'pm2 status hbox-firmware-server'"
if ($pm2Status -match "online") {
    Write-Host "[SUCCESS] PM2热部署完成" -ForegroundColor Green
    
    # 显示更多日志行（50行）
    Write-Host "[INFO] 显示最新日志..." -ForegroundColor Blue
    $logCheck = Invoke-Expression "$sshCmd 'cd $($EnvConfig.path) && pm2 logs hbox-firmware-server --lines 50'"
    Write-Host "最新日志:" -ForegroundColor Blue
    Write-Host $logCheck -ForegroundColor Gray
} else {
    Write-Host "[WARNING] PM2状态异常，请检查日志" -ForegroundColor Yellow
    
    # 显示错误日志
    Write-Host "[INFO] 显示错误日志..." -ForegroundColor Blue
    $errorLog = Invoke-Expression "$sshCmd 'cd $($EnvConfig.path) && pm2 logs hbox-firmware-server --err --lines 20'"
    Write-Host "错误日志:" -ForegroundColor Red
    Write-Host $errorLog -ForegroundColor Gray
}

# 3. 重启Nginx（如果需要）
Write-Host "[INFO] 步骤 3: 检查并重启Nginx..." -ForegroundColor Blue

# 检查Nginx配置
$nginxTest = Invoke-Expression "$sshCmd 'nginx -t'"
if ($LASTEXITCODE -eq 0) {
    # 重新加载Nginx配置
    Invoke-Expression "$sshCmd 'systemctl reload nginx'"
    Write-Host "[SUCCESS] Nginx配置已重新加载" -ForegroundColor Green
} else {
    Write-Host "[WARNING] Nginx配置测试失败，跳过重启" -ForegroundColor Yellow
}

# 4. 快速健康检查
Write-Host "[INFO] 步骤 4: 健康检查..." -ForegroundColor Blue

# 检查直接端口访问
try {
    $directResponse = Invoke-WebRequest -Uri "http://$($EnvConfig.host):$($EnvConfig.port)/health" -UseBasicParsing -TimeoutSec 5
    if ($directResponse.StatusCode -eq 200) {
        Write-Host "[SUCCESS] 直接端口访问正常" -ForegroundColor Green
    } else {
        Write-Host "[WARNING] 直接端口访问异常: $($directResponse.StatusCode)" -ForegroundColor Yellow
    }
}
catch {
    Write-Host "[WARNING] 直接端口访问检查失败" -ForegroundColor Yellow
}

# 检查域名访问
try {
    $domainResponse = Invoke-WebRequest -Uri "https://$($EnvConfig.domain)/health" -UseBasicParsing -TimeoutSec 5
    if ($domainResponse.StatusCode -eq 200) {
        Write-Host "[SUCCESS] 域名访问正常" -ForegroundColor Green
    } else {
        Write-Host "[WARNING] 域名访问异常: $($domainResponse.StatusCode)" -ForegroundColor Yellow
    }
}
catch {
    Write-Host "[WARNING] 域名访问检查失败" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "    快速部署完成！" -ForegroundColor White
Write-Host "========================================" -ForegroundColor Green
Write-Host "访问方式 1: http://$($EnvConfig.host):$($EnvConfig.port)" -ForegroundColor White
Write-Host "访问方式 2: https://$($EnvConfig.domain)" -ForegroundColor White
Write-Host ""

Write-Host "部署时间: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" -ForegroundColor Gray
Write-Host ""

Read-Host "按回车键退出..." 