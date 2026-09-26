# 해당코드는 Codex로 수정됨
param(
    [string]$VulkanHeaders = '',
    [string]$SdkRoot = "$env:LOCALAPPDATA\Android\Sdk",
    [string]$NdkVersion = '30.0.15729638',
    [string]$CMakeVersion = '4.1.2',
    [int]$AndroidApi = 29
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not $VulkanHeaders) {
    throw 'Pass the Vulkan-Headers v1.4.344 checkout with -VulkanHeaders.'
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$ndk = Join-Path $SdkRoot "ndk\$NdkVersion"
$cmake = Join-Path $SdkRoot "cmake\$CMakeVersion\bin\cmake.exe"
$ninja = Join-Path $SdkRoot "cmake\$CMakeVersion\bin\ninja.exe"
$toolchain = Join-Path $ndk 'build\cmake\android.toolchain.cmake'
$headers = Join-Path $VulkanHeaders 'include'
$coreHeader = Join-Path $headers 'vulkan\vulkan_core.h'
$vulkanLibrary = Join-Path $ndk "toolchains\llvm\prebuilt\windows-x86_64\sysroot\usr\lib\aarch64-linux-android\$AndroidApi\libvulkan.so"
$build = Join-Path $repoRoot 'build\android-arm64'

foreach ($path in @($cmake, $ninja, $toolchain, $coreHeader, $vulkanLibrary)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Required build input is missing: $path"
    }
}

& $cmake -S $repoRoot -B $build -G Ninja `
    "-DCMAKE_MAKE_PROGRAM=$ninja" `
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain" `
    '-DANDROID_ABI=arm64-v8a' `
    "-DANDROID_PLATFORM=android-$AndroidApi" `
    "-DVulkan_INCLUDE_DIR=$headers" `
    "-DVulkan_LIBRARY=$vulkanLibrary"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $cmake --build $build --config Release
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Output "Built $build\librdna3_micro_engine.a"
