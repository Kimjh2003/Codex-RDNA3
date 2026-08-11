# 해당코드는 Codex로 수정됨
param(
    [Parameter(Mandatory = $true)]
    [string]$IsaDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$rules = @(
    @{
        Name = "rdna_sve_sme_u64_ingress"
        Patterns = @("DS_", "S_BARRIER", "S_WAITCNT")
    },
    @{
        Name = "rdna3_pure_fp32_wave"
        Patterns = @("V_DUAL_", "V_MUL_F32", "V_ADD_F32", "V_FMA_F32")
    },
    @{
        Name = "rdna3_pure_fp16x2_wave"
        Patterns = @("V_PK_MUL_F16", "V_PK_ADD_F16", "V_PK_FMA_F16")
    },
    @{
        Name = "rdna3_fp16x2_fp32_mixed"
        Patterns = @("V_PK_MUL_F16", "V_DOT2_F32_F16", "V_FMA_MIX_F32")
    },
    @{
        Name = "rdna3_int8x4_int32_mixed"
        Patterns = @("V_DOT4_I32_IU8")
    }
)

foreach ($rule in $rules) {
    $candidate = Join-Path $IsaDirectory ($rule.Name + ".isa")
    if (-not (Test-Path -LiteralPath $candidate)) {
        Write-Warning "Missing optional AMD ISA dump: $candidate"
        continue
    }

    $text = (Get-Content -Raw -LiteralPath $candidate).ToUpperInvariant()
    $matches = @($rule.Patterns | Where-Object { $text.Contains($_) })
    if ($matches.Count -eq 0) {
        Write-Warning "$($rule.Name): no expected RDNA3 mnemonic was found"
    }
    else {
        Write-Host "$($rule.Name): $($matches -join ', ')"
    }
}

Write-Host "Note: VOPD is legal only for Wave32. Driver-generated ISA is authoritative."
