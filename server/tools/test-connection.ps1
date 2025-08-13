# STM32 HBox 固件服务器连接测试脚本
# 使用方法: .\test-connection.ps1 [环境名]

param(
    [string]$Environment = ""
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
    Write-Host "STM32 HBox 固件服务器连接测试脚本" -ForegroundColor $White
    Write-Host ""
    Write-Host "使用方法:" -ForegroundColor $White
    Write-Host "  .\test-connection.ps1 [环境名]" -ForegroundColor $White
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
    Write-Host "  .\test-connection.ps1 prod" -ForegroundColor $White
    Write-Host "  .\test-connection.ps1 dev" -ForegroundColor $White
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

# 测试SSH连接
function Test-SshConnection {
    Write-Info "测试SSH连接..."
    
    # 检查密钥文件
    if (-not (Test-Path $SshKey)) {
        Write-Error "SSH密钥文件不存在: $SshKey"
        return $false
    }
    
    # 设置.pem文件权限 (仅适用于WSL)
    if ($UseWsl -and $SshKey -match "\.pem$") {
        Write-Info "设置.pem密钥文件权限..."
        try {
            wsl chmod 600 $SshKey
            Write-Success "密钥文件权限设置完成"
        }
        catch {
            Write-Warning "无法设置密钥文件权限"
        }
    }
    
    # 测试连接
    Write-Info "连接到 $User@$ServerHost..."
    
    $testCmd = "echo 'SSH连接测试成功' && whoami && pwd && uname -a"
    
    try {
        if ($UseWsl) {
            $result = wsl ssh -i $SshKey -o StrictHostKeyChecking=no -o ConnectTimeout=10 "$User@$ServerHost" $testCmd 2>&1
        } else {
            $result = ssh -i $SshKey -o StrictHostKeyChecking=no -o ConnectTimeout=10 "$User@$ServerHost" $testCmd 2>&1
        }
        
        if ($LASTEXITCODE -eq 0) {
            Write-Success "SSH连接测试成功！"
            Write-Host "服务器信息:" -ForegroundColor $White
            Write-Host $result -ForegroundColor $Yellow
            return $true
        } else {
            Write-Error "SSH连接测试失败"
            Write-Host "错误信息:" -ForegroundColor $Red
            Write-Host $result -ForegroundColor $Red
            return $false
        }
    }
    catch {
        Write-Error "SSH连接测试异常: $($_.Exception.Message)"
        return $false
    }
}

# 测试端口连接
function Test-PortConnection {
    Write-Info "测试端口连接..."
    
    try {
        $tcpClient = New-Object System.Net.Sockets.TcpClient
        $connect = $tcpClient.BeginConnect($ServerHost, $Port, $null, $null)
        $wait = $connect.AsyncWaitHandle.WaitOne(5000, $false)
        
        if ($wait) {
            $tcpClient.EndConnect($connect)
            Write-Success "端口 $Port 连接成功"
            $tcpClient.Close()
            return $true
        } else {
            Write-Warning "端口 $Port 连接超时或失败"
            $tcpClient.Close()
            return $false
        }
    }
    catch {
        Write-Warning "端口 $Port 连接测试异常: $($_.Exception.Message)"
        return $false
    }
}

# 测试服务器环境
function Test-ServerEnvironment {
    Write-Info "测试服务器环境..."
    
    $envTests = @(
        @{ name = "Node.js"; cmd = "node --version" },
        @{ name = "npm"; cmd = "npm --version" },
        @{ name = "7-Zip"; cmd = "7z --version" },
        @{ name = "系统信息"; cmd = "cat /etc/os-release" }
    )
    
    foreach ($test in $envTests) {
        try {
            if ($UseWsl) {
                $result = wsl ssh -i $SshKey -o StrictHostKeyChecking=no "$User@$ServerHost" $test.cmd 2>&1
            } else {
                $result = ssh -i $SshKey -o StrictHostKeyChecking=no "$User@$ServerHost" $test.cmd 2>&1
            }
            
            if ($LASTEXITCODE -eq 0) {
                Write-Success "$($test.name): 正常"
                Write-Host "  $result" -ForegroundColor $Yellow
            } else {
                Write-Warning "$($test.name): 未找到或异常"
            }
        }
        catch {
            Write-Warning "$($test.name): 测试失败"
        }
    }
}

# 主函数
function Main {
    # 检查参数
    if ($Environment -eq "-h" -or $Environment -eq "--help") {
        Show-Help
        exit 0
    }
    
    if (-not $Environment) {
        Write-Error "请提供环境名参数"
        Show-Help
        exit 1
    }
    
    Write-Info "STM32 HBox 固件服务器连接测试"
    Write-Host ""
    
    # 检查SSH工具
    Test-SshTool
    
    # 加载配置
    Load-Config $Environment
    
    Write-Info "测试环境: $Environment ($Description)"
    Write-Info "服务器: $User@$ServerHost"
    Write-Info "端口: $Port"
    Write-Info "密钥: $SshKey"
    Write-Host ""
    
    # 执行测试
    $sshSuccess = Test-SshConnection
    $portSuccess = Test-PortConnection
    
    if ($sshSuccess) {
        Test-ServerEnvironment
    }
    
    Write-Host ""
    if ($sshSuccess) {
        Write-Success "连接测试完成！服务器可以正常访问。"
    } else {
        Write-Error "连接测试失败！请检查网络连接和SSH配置。"
    }
}

# 执行主函数
Main 