$ErrorActionPreference = "Stop"

$ProjectDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$Sdk = "E:/Works/CH585EVT/EVT/EXAM"

if ($env:CH585_SDK -and $env:CH585_SDK.Trim().Length -gt 0) {
    $Sdk = $env:CH585_SDK
}

Write-Host "Project: $ProjectDir"
Write-Host "SDK:     $Sdk"

Push-Location $ProjectDir
try {
    $makeCmd = Get-Command make -ErrorAction SilentlyContinue
    if (-not $makeCmd) {
        $makeCmd = Get-Command mingw32-make -ErrorAction SilentlyContinue
    }

    if (-not $makeCmd) {
        throw "make or mingw32-make not found. Please install GNU Make."
    }

    $args = @("SDK=$Sdk", "all")
    & $makeCmd.Source @args
}
finally {
    Pop-Location
}
