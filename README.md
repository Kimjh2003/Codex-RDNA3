> 해당코드는 Codex로 수정됨

# RDNA3 Vulkan compute demos

이 저장소에는 두 개의 독립적인 Vulkan compute 데모가 있다.

| 위치 | 대상 | 검증 범위 |
| --- | --- | --- |
| [`src/`](src/) + [`shaders/`](shaders/) | AMD RDNA3용 논리 micro-engine | SPIR-V 계약과 수치·스케줄링 테스트. RDNA3 GPU 실행은 미검증 |
| [`mobile/xclipse940-vulkan-kernel/`](mobile/xclipse940-vulkan-kernel/README.md) | Samsung Xclipse 940 / Android Vulkan 1.3 | Galaxy S24 실기기에서 ASTC 입력과 GPU 출력 검증 |

두 데모 모두 Vulkan으로 독립 구현했으며 ROCm/HIP/ROCr 런타임을 포함하지 않는다.

## 저장소 구성

- [`src/`](src/): RDNA3 호스트 라이브러리
- [`shaders/`](shaders/): Slang compute 셰이더 소스
- [`prebuilt/`](prebuilt/): 2026-08-12에 검증한 SPIR-V 및 어셈블리와 바이너리 체크섬
- [`scripts/`](scripts/), [`tests/`](tests/): 컴파일·ISA 점검 스크립트와 Python 테스트
- [`docs/`](docs/): API·표준 대응과 당시 검증 기록
- [`mobile/`](mobile/): Xclipse 940 모바일 데모

## RDNA3 논리 micro-engine

지금까지 만든 계산 경로를 하나의 Vulkan compute 패키지로 병합한 예시다.

- API/SDK 기준: Vulkan 1.4, 헤더 `VK_HEADER_VERSION >= 344`
- 중간 표현: SPIR-V 1.6
- GPU 타깃: AMD RDNA3
- 실행 폭: required subgroup size를 이용한 Wave32 + Wave64
- 스케줄링: MES 문서의 큐 상태, 4단계 우선순위, quantum, doorbell, 상태 조회, 원형 로그를 사용자 공간에서 모델링

이 코드는 MES 펌웨어나 KMD를 대체하지 않는다. Vulkan 애플리케이션이 직접 사용할 수 있는 논리 스케줄러이며, 최종 작업은 `vkCmdDispatch`로 기록된다.

## 병합된 커널

| `KernelKind` | 입력 → 출력 | RDNA3 의도 |
|---|---|---|
| `sveSmeU64Ingress` | SVE/SME `uint64` lane 2–32개 → 64개의 `uint32` word plane + 재결합 `uint64` | shaderInt64 없이 LDS에서 Wave32/64 공통 배치 |
| `pureFp32` | FP32 → FP32 제곱+2 및 subgroup 합 | precise FP32 VALU, Wave32에서 합법적인 VOPD 선택 가능성 |
| `pureFp16x2` | packed FP16x2 → packed FP16x2 및 subgroup 합 | VOP3P `V_PK_*_F16` 후보, 계산·저장·합 모두 FP16 |
| `fp16x2Fp32Mixed` | packed FP16x2 → FP16 제곱 → FP32 합 | FP16 계산 + FP32 reduction mixed 경로 |
| `int8x4Int32Mixed` | packed signed INT8x4 두 개 + INT32 bias → INT32 | SPIR-V packed `OpSDot`, RDNA3 `V_DOT4_I32_IU8` 후보 |

SPIR-V가 특정 RDNA3 기계 명령을 강제하지는 않는다. 실제 선택은 AMD 드라이버가 만든 ISA dump로 확인해야 한다.

## 스케줄러 동작

논리 큐 상태는 `unmapped → mappedDisconnected → mappedConnected`로 움직인다. 작업을 넣으면 논리 doorbell이 증가하고, 제한된 매핑 슬롯이 가득 찬 경우 idle 큐 또는 더 낮은 우선순위의 disconnected 큐를 unmap한다.

우선순위는 `realtime > focus > normal > idle`의 strict ordering이다. 같은 단계에서는 `quantumDispatches`개씩 실행한 뒤 round-robin한다. `automatic` Wave 정책은 RDNA3의 VOPD가 Wave32 전용이라는 ISA 제약 때문에 Wave32를 선택하며, 모든 커널은 Wave64를 명시적으로 선택할 수도 있다.

`JobFlags::barrierBefore`와 `barrierAfter`는 dispatch 사이에 Vulkan 1.4 `vkCmdPipelineBarrier2`를 넣어 storage write → compute read/write 의존성을 만든다. SVE/SME ingress 내부의 두 번의 `GroupMemoryBarrierWithGroupSync()`는 LDS word-plane 재배치의 work-group 동기화를 담당한다.

## 빌드와 검증

Slang 셰이더를 컴파일한다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\compile_all.ps1 `
  -SlangCompiler C:\path\to\slangc.exe
```

C++ 라이브러리는 Vulkan SDK 1.4.344 이상과 C++20 컴파일러가 필요하다.

```powershell
cmake -S . -B out
cmake --build out --config Release
```

Android Studio가 설치한 NDK 30·CMake 4.1.2·Ninja로 AArch64 정적 라이브러리도
교차 빌드할 수 있다. NDK 30의 Vulkan 헤더는 revision 335여서 이 저장소의
최소 요구치 344에 못 미친다. [공식 Vulkan-Headers v1.4.344](https://github.com/KhronosGroup/Vulkan-Headers/releases/tag/v1.4.344)를
별도로 준비한 뒤 해당 체크아웃 경로를 전달한다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build_android_arm64.ps1 `
  -VulkanHeaders C:\path\to\Vulkan-Headers
```

결과는 `build/android-arm64/librdna3_micro_engine.a`다. 이 빌드는 Android
AArch64용 C++ 라이브러리 생성까지 확인하며, RDNA3 GPU에서의 Vulkan 1.4
실행 검증은 포함하지 않는다. [빌드 기록](docs/validation-2026-09-27-android.md).

표준 라이브러리만 쓰는 수치·스케줄링·SPIR-V 계약 테스트:

```powershell
python .\tests\test_unified_micro_engine.py
```

AMD RGA 또는 드라이버 도구로 만든 `.isa` 파일을 검사하려면 각 build 이름과 같은 이름으로 모은 뒤 실행한다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\check_rdna3_isa.ps1 `
  -IsaDirectory C:\path\to\isa-dumps
```

## 호스트 연결 순서

1. `queryUnifiedDeviceSupport()`와 `requireUnifiedRdna3Support()`로 RDNA3 기능을 확인한다.
2. `RequiredDeviceFeatures::head()`를 `VkDeviceCreateInfo::pNext`에 연결한다.
3. 셰이더별 descriptor set layout과 최대 16바이트 push constant range를 만든다.
4. `MicroEngineScheduler`를 만들고 5개 shader module/layout을 `registerKernel()`로 등록한다.
5. `addQueue()`, `enqueue()` 후 command buffer 안에서 `recordBatch()`를 부른다.
6. 일반 Vulkan semaphore/fence/timeline semaphore로 제출 완료를 추적한다.

세부 필드와 descriptor binding은 [API와 레이아웃](docs/api-and-layouts.md),
표준 문서와의 대응은 [표준 대응](docs/standards-mapping.md),
당시 검증 결과는 [검증 기록](docs/validation-2026-08-12.md)을 참고한다.

## 라이선스

저장소는 [Apache License 2.0](LICENSE)으로 공개한다. 모바일 데모의
독립 배포를 위한 라이선스와 저작권 표시는 해당 디렉터리에도 있다.
