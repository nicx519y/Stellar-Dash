# STM32 HBox 固件服务器文件检查脚本
# 检查所有必需的脚本文件是否存在

Write-Host "检查部署工具文件..." -ForegroundColor Blue
Write-Host ""

$ScriptDir = $PSScriptRoot
$files = @(
    "deploy.ps1",
    "test-connection.ps1", 
    "service-manager.ps1",
    "setup.ps1",
    "deploy-config.json"
)

$allExist = $true

foreach ($file in $files) {
    $filePath = Join-Path $ScriptDir $file
    if (Test-Path $filePath) {
        Write-Host "✓ $file" -ForegroundColor Green
    } else {
        Write-Host "✗ $file (未找到)" -ForegroundColor Red
        $allExist = $false
    }
}

Write-Host ""
if ($allExist) {
    Write-Host "所有文件检查通过！" -ForegroundColor Green
} else {
    Write-Host "部分文件缺失，请检查文件完整性。" -ForegroundColor Red
}

Write-Host ""
Write-Host "当前目录: $ScriptDir" -ForegroundColor Yellow
Write-Host ""

# 列出目录中的所有文件
Write-Host "目录中的所有文件:" -ForegroundColor Blue
Get-ChildItem $ScriptDir -Name | Sort-Object 