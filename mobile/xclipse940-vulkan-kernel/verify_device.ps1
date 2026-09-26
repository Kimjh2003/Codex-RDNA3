param(
    [string]$NdkRoot = "$env:LOCALAPPDATA\Android\Sdk\ndk\30.0.15729638"
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$fixture = Join-Path $root 'verify'
& (Join-Path $fixture 'build.ps1') -NdkRoot $NdkRoot
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$devices = & adb devices
if (($devices | Select-String '\s+device$').Count -ne 1) {
    throw 'Connect exactly one Android device with USB debugging enabled.'
}

$pushes = @(
    @{ Local = (Join-Path $fixture 'hpc_fused_demo'); Remote = 'xc_verify_demo' },
    @{ Local = (Join-Path $root 'fused_astc_u64.spv'); Remote = 'xc_verify_shader.spv' },
    @{ Local = (Join-Path $fixture 'source.astc'); Remote = 'xc_verify_source.astc' },
    @{ Local = (Join-Path $fixture 'golden.rgba'); Remote = 'xc_verify_golden.rgba' }
)
foreach ($item in $pushes) {
    & adb push $item.Local "/data/local/tmp/$($item.Remote)"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
& adb shell chmod 755 /data/local/tmp/xc_verify_demo
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$runOutput = & adb shell /data/local/tmp/xc_verify_demo `
    /data/local/tmp/xc_verify_source.astc `
    /data/local/tmp/xc_verify_shader.spv `
    /data/local/tmp/xc_verify_golden.rgba `
    /data/local/tmp/xc_verify_output.rgba `
    /data/local/tmp/xc_verify_word_planes.bin
$runExit = $LASTEXITCODE
$runOutput | Write-Output
if ($runExit -ne 0) { exit $runExit }
$runOutput | Set-Content -LiteralPath (Join-Path $fixture 'device_run.log') -Encoding utf8

& adb pull /data/local/tmp/xc_verify_output.rgba (Join-Path $fixture 'device_output.rgba')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& adb pull /data/local/tmp/xc_verify_word_planes.bin (Join-Path $fixture 'device_word_planes.bin')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$rgbaActual = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $fixture 'device_output.rgba')).Hash
$rgbaExpected = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $fixture 'golden.rgba')).Hash
$wordsActual = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $fixture 'device_word_planes.bin')).Hash
$wordsExpected = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $fixture 'expected_word_planes.bin')).Hash
if ($rgbaActual -ne $rgbaExpected -or $wordsActual -ne $wordsExpected) {
    throw 'Packaged shader output differs from verified fixture'
}
Write-Output 'PASS: packaged SPIR-V on device; RGBA and word-plane SHA256 match fixtures.'
