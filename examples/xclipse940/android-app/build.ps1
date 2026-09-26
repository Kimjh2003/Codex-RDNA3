param(
    [string]$SdkRoot = "$env:LOCALAPPDATA\Android\Sdk",
    [string]$Repository = (Join-Path $PSScriptRoot '..\..\..')
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path $PSScriptRoot).Path
$repo = (Resolve-Path $Repository).Path
$pack = Join-Path $repo 'examples\xclipse940'
$fixture = Join-Path $pack 'verify'
$tool = Join-Path $SdkRoot 'ndk\30.0.15729638\toolchains\llvm\prebuilt\windows-x86_64\bin\clang++.exe'
$buildTools = Join-Path $SdkRoot 'build-tools\36.0.0'
$jbr = 'C:\Program Files\Android\Android Studio\jbr\bin'
$env:JAVA_HOME = Split-Path $jbr -Parent
$env:Path = "$jbr;$env:Path"
$androidJar = Join-Path $SdkRoot 'platforms\android-36\android.jar'
$build = Join-Path $root 'build'
$assets = Join-Path $build 'assets'
$classes = Join-Path $build 'classes'
$stage = Join-Path $build 'stage'
New-Item -ItemType Directory -Force $assets, $classes, (Join-Path $stage 'lib\arm64-v8a') | Out-Null

$sources = @{
    'source.astc' = (Join-Path $fixture 'source.astc')
    'fused_astc_u64.spv' = (Join-Path $pack 'fused_astc_u64.spv')
    'golden.rgba' = (Join-Path $fixture 'golden.rgba')
    'expected_word_planes.bin' = (Join-Path $fixture 'expected_word_planes.bin')
}
foreach ($name in $sources.Keys) {
    Copy-Item -LiteralPath $sources[$name] -Destination (Join-Path $assets $name) -Force
}

$triple = '--target=aarch64-linux-android29'
& $tool $triple -std=c++17 -O2 '-march=armv8.2-a+sve2' -fPIC -c `
    (Join-Path $fixture 'sve_reference.cpp') -o (Join-Path $build 'sve_reference.o')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $tool $triple -std=c++17 -O2 -fPIC -Wno-missing-field-initializers `
    '-Dmain=xclipse_demo_main' -c (Join-Path $fixture 'hpc_fused_demo.cpp') `
    -o (Join-Path $build 'hpc_fused_demo.o')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $tool $triple -std=c++17 -O2 -fPIC -c (Join-Path $root 'native_bridge.cpp') `
    -o (Join-Path $build 'native_bridge.o')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $tool $triple -shared -static-libstdc++ `
    (Join-Path $build 'sve_reference.o') (Join-Path $build 'hpc_fused_demo.o') `
    (Join-Path $build 'native_bridge.o') -lvulkan -llog `
    -o (Join-Path $stage 'lib\arm64-v8a\libxclipse_demo.so')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& (Join-Path $jbr 'javac.exe') -source 8 -target 8 -Xlint:-options -cp $androidJar `
    -d $classes (Join-Path $root 'src\dev\kimjh\xclipsegpu\MainActivity.java')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& (Join-Path $buildTools 'd8.bat') --min-api 29 --lib $androidJar `
    --output $stage (Join-Path $classes 'dev\kimjh\xclipsegpu\MainActivity.class')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$unsigned = Join-Path $build 'unsigned.apk'
& (Join-Path $buildTools 'aapt2.exe') link -o $unsigned -I $androidJar `
    --manifest (Join-Path $root 'AndroidManifest.xml') -A $assets
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Push-Location $stage
try {
    & (Join-Path $buildTools 'aapt.exe') add $unsigned 'classes.dex' `
        'lib/arm64-v8a/libxclipse_demo.so'
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} finally { Pop-Location }

$aligned = Join-Path $build 'aligned.apk'
& (Join-Path $buildTools 'zipalign.exe') -f 4 $unsigned $aligned
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$key = Join-Path $build 'debug.keystore'
if (!(Test-Path -LiteralPath $key)) {
    & (Join-Path $jbr 'keytool.exe') -genkeypair -keystore $key `
        -storepass android -keypass android -alias androiddebugkey `
        -keyalg RSA -keysize 2048 -validity 3650 `
        -dname 'CN=Android Debug,O=Codex,C=US'
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
$apk = Join-Path $build 'xclipse-gpu-demo-debug.apk'
& (Join-Path $buildTools 'apksigner.bat') sign --ks $key `
    --ks-pass pass:android --key-pass pass:android --out $apk $aligned
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& (Join-Path $buildTools 'apksigner.bat') verify --verbose $apk
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Output "Built $apk"
