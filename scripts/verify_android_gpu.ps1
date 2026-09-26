# 해당코드는 Codex로 수정됨
param(
    [string]$VulkanHeaders = '',
    [string]$SdkRoot = "$env:LOCALAPPDATA\Android\Sdk",
    [switch]$BuildOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$ndk = Join-Path $SdkRoot 'ndk\30.0.15729638'
$clang = Join-Path $ndk 'toolchains\llvm\prebuilt\windows-x86_64\bin\clang++.exe'
$headers = if ($VulkanHeaders) {
    Join-Path (Resolve-Path -LiteralPath $VulkanHeaders).Path 'include'
} else {
    Join-Path $ndk 'toolchains\llvm\prebuilt\windows-x86_64\sysroot\usr\include'
}
$build = Join-Path $root 'build\android-arm64'
$archive = Join-Path $build 'librdna3_micro_engine.a'
$runner = Join-Path $build 'rdna3_gpu_runner'

& (Join-Path $PSScriptRoot 'build_android_arm64.ps1') `
    -VulkanHeaders $VulkanHeaders -SdkRoot $SdkRoot
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $clang --target=aarch64-linux-android33 -std=c++20 -O2 `
    -Wno-missing-field-initializers `
    -I (Join-Path $root 'src') `
    -I $headers `
    (Join-Path $root 'tests\rdna3_gpu_runner.cpp') $archive `
    -static-libstdc++ -lvulkan -o $runner
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Output "Built $runner"
if ($BuildOnly) { exit 0 }

$devices = & adb devices
if (@($devices | Select-String '\s+device$').Count -ne 1) {
    throw 'Connect exactly one Android device with USB debugging enabled.'
}
$remote = '/data/local/tmp/rdna3_gpu'
& adb shell mkdir -p $remote
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& adb push $runner "$remote/rdna3_gpu_runner"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& adb shell chmod 755 "$remote/rdna3_gpu_runner"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Get-ChildItem -LiteralPath (Join-Path $root 'prebuilt') -File |
    Where-Object { $_.Extension -eq '.spv' } | ForEach-Object {
    & adb push $_.FullName "$remote/$($_.Name)"
    if ($LASTEXITCODE -ne 0) { throw "adb push failed: $($_.Name)" }
}
$result = & adb shell "$remote/rdna3_gpu_runner" $remote 2>&1
$exitCode = $LASTEXITCODE
$result | Write-Output
$result | Set-Content -LiteralPath (Join-Path $build 'device_gpu_run.log') -Encoding utf8
if ($exitCode -ne 0) { exit $exitCode }
if (@($result | Select-String '^PASS: 5 original RDNA3 shaders x Wave32/Wave64 = 10 GPU dispatches').Count -ne 1) {
    throw 'GPU runner exited without the ten-dispatch PASS marker.'
}
