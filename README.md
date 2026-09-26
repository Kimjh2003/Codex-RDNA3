> 해당코드는 Codex로 수정됨

# Xclipse 940 Vulkan compute 데모

Galaxy S24의 Xclipse 940에서 실행해 본 **두 개의 Vulkan 1.3 compute 데모**다. 첫 번째는 RDNA3용으로 설계한 논리 스케줄러와 커널 5개를 Wave32·Wave64로 실행한다. 두 번째는 ASTC 이미지를 읽어 64비트 레인과 word-plane으로 변환한다.

**ROCm 포팅을 실험하기 위한 독립 구현**이다. ROCm/HIP/ROCr 런타임이나 AMD 드라이버를 포함하지 않으며, AMD Radeon에서의 실행은 아직 검증하지 않았다.

## 빠른 시작: 폰에서 두 데모 실행

Windows PowerShell에서 이 저장소의 루트로 이동해 실행한다. Android Studio의 **SDK Platform-Tools, NDK 30.0.15729638, CMake 4.1.2**가 필요하다. `adb`가 PATH에 있어야 하고, 폰의 USB 디버깅을 허용해야 한다.

```powershell
adb devices
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\verify_all_on_device.ps1
```

`adb devices`에 `device` 상태의 기기 **한 대**가 보여야 한다. SDK가 기본 위치가 아니라면 두 번째 명령 끝에 `-SdkRoot C:\path\to\Android\Sdk`를 붙인다.

성공하면 `10 GPU dispatches`와 `packaged SPIR-V on device`의 PASS 줄이 나온다. 마지막 줄은 `PASS: both on-device Vulkan demos completed.`다. 첫 데모의 실행 로그는 `build/android-arm64/device_gpu_run.log`, 두 번째 로그는 `examples/xclipse940/verify/device_run.log`에 저장된다.

## 원하는 것만 실행

| 목적 | 명령 또는 안내 |
| --- | --- |
| 스케줄러와 커널 5개를 GPU에서 검증 | `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\verify_android_gpu.ps1` |
| ASTC → word-plane 커널을 GPU에서 검증 | `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\examples\xclipse940\verify_device.ps1` |
| 폰 없이 AArch64 라이브러리 빌드 | `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\build_android_arm64.ps1` |
| 수치·스케줄링·SPIR-V 계약 점검 | `python .\tests\test_unified_micro_engine.py` |
| Android 앱으로 실행하거나 Sokatoa에서 추적 | [앱 안내](examples/xclipse940/android-app/README.md) |

셰이더를 다시 컴파일하려면 Slang `slangc`를 준비하고 `scripts/compile_all.ps1 -SlangCompiler C:\path\to\slangc.exe`를 실행한다. 원본 SPIR-V는 이미 저장소에 들어 있다.

## 파일 찾기

| 위치 | 내용 |
| --- | --- |
| [`engine/`](engine/) | `MicroEngineScheduler` C++ 라이브러리와 Vulkan 기능 검사 |
| [`kernels/source/`](kernels/source/) | Slang 셰이더 소스 5개 |
| [`kernels/compiled/`](kernels/compiled/) | 대응하는 SPIR-V 1.6 바이너리·어셈블리·SHA-256 목록 |
| [`examples/xclipse940/`](examples/xclipse940/README.md) | ASTC 데모, 입력·기준값, 실기기 검증, Android 앱 |
| [`scripts/`](scripts/) | 빌드·실기기 실행·ISA 확인 명령 |
| [`tests/`](tests/) | CPU 수치 참조와 GPU 실행기 |
| [`docs/reference/`](docs/reference/) | API 레이아웃과 표준 대응 |
| [`docs/results/`](docs/results/) | 날짜별 빌드·실기기 검증 기록 |

## 무엇을 검증했나

2026-09-27 Galaxy S24 SM-S921N / Samsung Xclipse 940 / Vulkan 1.3.304에서 원본 커널 5개를 Wave32와 Wave64로 각각 제출해 **10번의 GPU dispatch**를 검증했다. ASTC 데모는 256×256 ASTC 8×8 sRGB 이미지를 한 번의 dispatch로 처리하고 RGBA·word-plane 출력을 기준값과 SHA-256으로 비교했다. [GPU 실행 기록](docs/results/validation-2026-09-27-xclipse-rdna3-gpu.md)과 [ASTC 실기기 결과](examples/xclipse940/DEVICE_RESULT.txt)에 세부 정보가 있다.

이 저장소의 Wave32/64 선택과 큐 우선순위는 **Vulkan 애플리케이션의 논리 스케줄링**이다. MES 펌웨어나 KMD를 교체하지 않는다. SPIR-V의 연산 형태가 특정 RDNA3 기계 명령의 선택을 보장하지도 않는다. 그 확인에는 실제 AMD 드라이버의 ISA dump가 필요하다.

통합 방법은 [API와 descriptor 레이아웃](docs/reference/api-and-layouts.md), 관련 규격은 [표준 대응](docs/reference/standards-mapping.md), 모바일 커널의 입출력 계약은 [Xclipse 940 예제 안내](examples/xclipse940/README.md)를 참고하면 된다.

## 라이선스

[Apache License 2.0](LICENSE). 모바일 예제 안에도 독립 배포를 위한 라이선스와 저작권 표시가 있다.
