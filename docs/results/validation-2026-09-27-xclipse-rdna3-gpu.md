> 해당코드는 Codex로 수정됨

# RDNA3 micro-engine의 Xclipse 940 GPU 실행 검증

검증일: 2026-09-27 (Asia/Seoul)

## 대상과 경계

- 기기: Galaxy S24 `SM-S921N`, Samsung Xclipse 940, vendor ID `0x144d`
- Vulkan **런타임 1.3.304**, NDK 빌드 헤더 **revision 335**
- 빌드: Android NDK 30, `arm64-v8a`; 실행 파일은 API 33을 대상으로 링크
- 실행 코드: `engine/rdna3_micro_engine.cpp`의 **원본 `MicroEngineScheduler`**
- 커널: `kernels/compiled/`에 저장된 원본 SPIR-V 1.6 파일 다섯 개

`requireUnifiedRdna3Support()`는 Vulkan 1.3 이상에서 AMD GPU 또는 Samsung
Xclipse 940을 허용하며, Wave32·Wave64, FP16/16-bit storage, subgroup 연산,
packed signed INT8 dot, synchronization2와 shaderInt16을 확인한다. 실행
검증은 이 **본체 기능 검사 함수를 통과한 뒤** 원본 스케줄러에 다섯 셰이더를
등록했다. 셰이더와 스케줄러의 dispatch 로직은 변경하지 않았다.

## 실기기 결과

각 커널을 Wave32와 Wave64로 한 번씩 등록·제출했다. 매 작업마다 Vulkan
fence 완료를 기다리고 GPU storage buffer를 CPU 기준값과 비교했다.

| 원본 커널 | Wave32 불일치 | Wave64 불일치 |
| --- | ---: | ---: |
| `sveSmeU64Ingress` | 0 | 0 |
| `pureFp32` | 0 | 0 |
| `pureFp16x2` | 0 | 0 |
| `fp16x2Fp32Mixed` | 0 | 0 |
| `int8x4Int32Mixed` | 0 | 0 |

**총 10개 `vkCmdDispatch`와 CPU 출력 비교 통과.** 스케줄러 로그에도
`recordDispatch` 이벤트 10개가 기록됐다. `vkCreateComputePipelines`, command
recording, `vkQueueSubmit`, fence wait, GPU readback을 실행했다. 이는 기존
모바일 ASTC 커널 데모와 별개인 원본 RDNA3 micro-engine의 GPU 통합 검사다.

## 재현

Android SDK/NDK, USB 디버깅을 허용한 기기를 준비한 뒤
저장소 루트에서 실행한다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\verify_android_gpu.ps1
```

스크립트는 AArch64 정적 라이브러리와 GPU 검증 실행 파일을 빌드하고, 원본
SPIR-V 다섯 개를 기기로 전송해 실행한다. 실행 로그는
`build/android-arm64/device_gpu_run.log`에 저장된다. 폰 없이 빌드만 확인하려면
`-BuildOnly`를 붙인다.

## 남은 범위

Xclipse 940의 Vulkan 1.3 본체 경로를 검증한 결과다. AMD Radeon GPU에서의
실행, ROCm/HIP/ROCr 실행, AMD 드라이버 최종 ISA 및 성능 향상은 검증하지 않았다.
