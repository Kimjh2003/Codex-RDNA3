# 해당코드는 Codex로 수정됨
param(
    [string]$SdkRoot = "$env:LOCALAPPDATA\Android\Sdk"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$ndk = Join-Path $SdkRoot 'ndk\30.0.15729638'

if (-not (Get-Command adb -ErrorAction SilentlyContinue)) {
    throw 'adb was not found. Add Android SDK platform-tools to PATH.'
}
if (-not (Test-Path -LiteralPath $ndk)) {
    throw "Android NDK 30.0.15729638 was not found: $ndk"
}
$devices = & adb devices
if (@($devices | Select-String '\s+device$').Count -ne 1) {
    throw 'Connect exactly one Android device and allow USB debugging.'
}

Write-Output '[1/2] RDNA3 micro-engine: five kernels x Wave32/Wave64'
& (Join-Path $PSScriptRoot 'verify_android_gpu.ps1') -SdkRoot $SdkRoot
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Output '[2/2] Xclipse 940: ASTC image to word planes'
& (Join-Path $root 'examples\xclipse940\verify_device.ps1') -NdkRoot $ndk
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Output 'PASS: both on-device Vulkan demos completed.'
