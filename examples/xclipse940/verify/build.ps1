param(
    [string]$NdkRoot = "$env:LOCALAPPDATA\Android\Sdk\ndk\30.0.15729638"
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$compiler = Join-Path $NdkRoot 'toolchains\llvm\prebuilt\windows-x86_64\bin\clang++.exe'
if (!(Test-Path -LiteralPath $compiler)) { throw "NDK compiler missing: $compiler" }
& $compiler --target=aarch64-linux-android29 -std=c++17 -O2 `
    '-march=armv8.2-a+sve2' -c (Join-Path $root 'sve_reference.cpp') `
    -o (Join-Path $root 'sve_reference.o')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $compiler --target=aarch64-linux-android29 -std=c++17 -O2 `
    -Wno-missing-field-initializers -static-libstdc++ `
    (Join-Path $root 'hpc_fused_demo.cpp') (Join-Path $root 'sve_reference.o') `
    -o (Join-Path $root 'hpc_fused_demo') -lvulkan
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Output "Built $root\hpc_fused_demo"
