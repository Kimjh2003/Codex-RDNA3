> 해당코드는 Codex로 수정됨

# Validation report

검증일: 2026-08-12 (Asia/Seoul)

이 문서는 최초 데모의 당시 검증 기록이다. 아래 명령과 파일 경로는
2026-09-27 저장소 구조 정리에 맞춰 갱신했다.

## 통과

- Slang 2026.14로 5개 compute shader를 SPIR-V 1.6 binary와 assembly로 실컴파일
- 모든 컴파일에 `-validate-ir` 적용
- 모든 entry point가 `LocalSize 64 1 1`
- SVE/SME ingress: `OpControlBarrier` 2개, native `OpTypeInt 64` 없음
- pure FP32: `OpTypeFloat 32`, subgroup `OpGroupNonUniformFAdd`, multiply/add `NoContraction`, FP16 타입 없음
- pure FP16x2: `Float16`, `v2half`, vector FP16 multiply/add, subgroup FP16x2 reduction, FP32 타입 없음
- FP16x2/FP32 mixed: FP16 vector multiply 뒤 `OpFConvert`와 FP32 add 확인
- INT8x4/INT32 mixed: `DotProductInput4x8BitPacked`, packed `OpSDot`, INT32 bias add, INT8 scalar 타입 없음
- Python 수치·layout·scheduler policy·source/SPIR-V contract 테스트 8개 통과
- 모든 배포용 소스/문서에 `해당코드는 Codex로 수정됨` 표시 확인

## 명령

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\compile_all.ps1 `
  -SlangCompiler C:\path\to\slangc.exe

python .\tests\test_unified_micro_engine.py
```

## 당시 환경에서 미실행

- Vulkan SDK header/library와 C++ compiler가 설치돼 있지 않아 host static library의 실제 C++ compile/link는 수행하지 못함
- RDNA3 실장 GPU에서 pipeline 생성, command recording, queue submit은 수행하지 못함
- AMD driver 최종 ISA dump가 없어 `V_PK_*_F16`, `V_DOT4_I32_IU8`, `V_DUAL_*`, `S_WAITCNT`, `S_BARRIER` 선택은 미확인

SPIR-V capability와 연산 타입이 맞는 것은 확인했지만 특정 RDNA3 machine instruction 선택을 보장한다는 뜻은 아니다. `scripts/check_rdna3_isa.ps1`로 실제 ISA dump를 별도 검사해야 한다.
