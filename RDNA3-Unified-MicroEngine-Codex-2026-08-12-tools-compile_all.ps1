# 해당코드는 Codex로 수정됨
param(
    [string]$SlangCompiler = "",
    [string]$OutputDirectory = "build"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-SlangCompiler([string]$ExplicitPath) {
    if ($ExplicitPath) {
        if (-not (Test-Path -LiteralPath $ExplicitPath)) {
            throw "Slang compiler does not exist: $ExplicitPath"
        }
        return (Resolve-Path -LiteralPath $ExplicitPath).Path
    }

    $fromPath = Get-Command slangc -ErrorAction SilentlyContinue
    if ($null -ne $fromPath) {
        return $fromPath.Source
    }

    if (Test-Path Env:VULKAN_SDK) {
        $candidate = Join-Path $env:VULKAN_SDK "Bin\slangc.exe"
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }

    throw "slangc was not found. Install Slang and pass -SlangCompiler."
}

function Assert-Contains([string]$Text, [string]$Pattern, [string]$Name) {
    if (-not $Text.Contains($Pattern)) {
        throw "$Name is missing required SPIR-V marker: $Pattern"
    }
}

function Assert-NotContains([string]$Text, [string]$Pattern, [string]$Name) {
    if ($Text.Contains($Pattern)) {
        throw "$Name contains forbidden SPIR-V marker: $Pattern"
    }
}

$slangc = Resolve-SlangCompiler $SlangCompiler
$outputRoot = Join-Path $PSScriptRoot $OutputDirectory
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

$shaders = @(
    @{
        Source = "shaders-rdna_sve_sme_u64_ingress.slang"
        Name = "rdna_sve_sme_u64_ingress"
        Capability = "spirv_1_6+spvGroupNonUniform"
        Extra = @()
        Required = @("OpControlBarrier", "OpTypeArray")
        Forbidden = @("OpTypeInt 64")
    },
    @{
        Source = "shaders-rdna3_pure_fp32_wave.slang"
        Name = "rdna3_pure_fp32_wave"
        Capability = "spirv_1_6+spvGroupNonUniform+spvGroupNonUniformArithmetic"
        Extra = @("-fp-mode", "precise")
        Required = @(
            "OpCapability GroupNonUniformArithmetic",
            "OpTypeFloat 32",
            "OpGroupNonUniformFAdd",
            "NoContraction")
        Forbidden = @("OpTypeFloat 16")
    },
    @{
        Source = "shaders-rdna3_pure_fp16x2_wave.slang"
        Name = "rdna3_pure_fp16x2_wave"
        Capability = "spirv_1_6+spvGroupNonUniform+spvGroupNonUniformArithmetic"
        Extra = @()
        Required = @(
            "OpCapability Float16",
            "OpTypeFloat 16",
            "OpTypeVector %half 2",
            "OpGroupNonUniformFAdd")
        Forbidden = @("OpTypeFloat 32")
    },
    @{
        Source = "shaders-rdna3_fp16x2_fp32_mixed.slang"
        Name = "rdna3_fp16x2_fp32_mixed"
        Capability = "spirv_1_6"
        Extra = @()
        Required = @(
            "OpCapability Float16",
            "OpTypeFloat 16",
            "OpTypeFloat 32",
            "OpFMul %v2half",
            "OpFConvert",
            "OpFAdd %float")
        Forbidden = @()
    },
    @{
        Source = "shaders-rdna3_int8x4_int32_mixed.slang"
        Name = "rdna3_int8x4_int32_mixed"
        Capability = "spirv_1_6"
        Extra = @()
        Required = @(
            "OpCapability DotProduct",
            "OpCapability DotProductInput4x8BitPacked",
            "OpSDot",
            "PackedVectorFormat4x8Bit",
            "OpIAdd %int")
        Forbidden = @("OpTypeInt 8")
    }
)

foreach ($shader in $shaders) {
    $source = Join-Path $PSScriptRoot $shader.Source
    $spv = Join-Path $outputRoot ($shader.Name + ".spv")
    $assembly = Join-Path $outputRoot ($shader.Name + ".spv-asm")

    & $slangc $source `
        -entry main -stage compute -target spirv `
        -capability $shader.Capability `
        -validate-ir @($shader.Extra) -o $spv
    if ($LASTEXITCODE -ne 0) {
        throw "SPIR-V compilation failed: $($shader.Source)"
    }

    & $slangc $source `
        -entry main -stage compute -target spirv-asm `
        -capability $shader.Capability `
        -validate-ir @($shader.Extra) -o $assembly
    if ($LASTEXITCODE -ne 0) {
        throw "SPIR-V assembly generation failed: $($shader.Source)"
    }

    $text = Get-Content -Raw -LiteralPath $assembly
    Assert-Contains $text "; Version: 1.6" $shader.Name
    foreach ($pattern in $shader.Required) {
        Assert-Contains $text $pattern $shader.Name
    }
    foreach ($pattern in $shader.Forbidden) {
        Assert-NotContains $text $pattern $shader.Name
    }
    Write-Host "Validated $($shader.Name) -> SPIR-V 1.6"
}
