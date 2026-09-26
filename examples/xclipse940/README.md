# Xclipse 940 ASTC Vulkan GPU 데모

## 먼저 실행하기

저장소 루트에서 Windows PowerShell로 실행한다. Android SDK의 `adb`, NDK
30.0.15729638과 USB 디버깅이 허용된 폰 한 대가 필요하다.

```powershell
adb devices
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\examples\xclipse940\verify_device.ps1
```

성공하면 `PASS: packaged SPIR-V on device; RGBA and word-plane SHA256 match fixtures.`가
나온다. 자세한 실행 로그는 `verify/device_run.log`에 기록된다. 폰 없이 파일을
살펴보려면 `fused_astc_u64.comp`(셰이더 소스), `kernel_abi.json`(입출력 규약),
`verify/source.png`(예제 이미지) 순서로 보면 된다.

이 패키지는 **GPU 안의 연산만** 담는다. 검증된 Vulkan compute shader 하나가
ASTC 8×8 sRGB 텍스처를 읽고, 인접한 두 픽셀을 64비트 레인으로 묶어
32레인/타일의 word-plane 출력으로 바꾼다. 출력의 `uint64` 연산은
`value XOR (value << 1)`이며, 이미지 전체를 한 dispatch로 처리한다.

## 포함물

- `fused_astc_u64.spv`: Android Vulkan 1.3용 SPIR-V 커널
- `fused_astc_u64.comp`: 재빌드 가능한 GLSL 소스
- `kernel_abi.json`: descriptor, push constant, 버퍼 크기와 word 순서
- `xclipse_gpu_kernel_abi.h`: 이미지 크기에서 버퍼 크기·workgroup 수를 계산하는 C 헬퍼
- `DEVICE_RESULT.txt`: Xclipse 940 실기기 결과
- `LICENSE`, `NOTICE`, `SHA256SUMS.txt`: Apache 2.0 원문·저작권 표시·입력과 커널의 체크섬
- `verify_device.ps1`, `verify/`: 팩에 포함된 SPIR-V를 실기기에서 재실행하는
  테스트 소스와 입력·기준값. GPU 커널 API에는 포함되지 않는다.
- `android-app/`: 같은 검증 경로를 디버그 APK로 실행해 Sokatoa의 GFXR
  트레이스에서 Vulkan compute dispatch를 볼 수 있는 래퍼.

검증용 Android 실행 파일은 저장소에 넣지 않았다. `verify_device.ps1`이 로컬
Android NDK로 `verify/hpc_fused_demo.cpp`와 SVE 기준값 코드를 빌드한다.
`verify/source.png`는 `verify/make_image.py`로 생성한 그림이고,
`verify/source.astc`는 이를 ASTC 8×8 sRGB로 압축한 입력이다.

입력은 **Vulkan sampled ASTC image**다. 출력은 필수 storage buffer 두 개다.
binding 1은 검증용 RGBA 사본, binding 2는 word-plane 데이터다.
`kernel_abi.json`의 layout과 동기화 규칙을 지켜 호출해야 한다.
`xclipse_gpu_kernel_abi.h`는 CPU에서 계산 크기만 정하며 픽셀 연산은 하지 않는다.

## LiteRT / S.LSI ENN과의 관계

앱이 LiteRT나 ENN을 함께 쓰더라도 이 커널은 **별도의 Vulkan GPU 단계**로
호출할 수 있다. 프레임워크가 이 GPU 버퍼를 입력으로 직접 받을 수 있는지,
필요하면 어떤 복사가 필요한지는 해당 런타임과 기기에서 별도로 확인해야 한다.
이 팩은 LiteRT custom op, GPU delegate, ENN delegate 또는 NPU 실행 파일을
제공하거나 호환을 주장하지 않는다. 텐서 형상/자료형을 모델 입력에 맞추는
어댑터도 포함하지 않는다.

## 검증 범위

2026-09-27 Galaxy S24 SM-S921N의 Samsung Xclipse 940 / Vulkan 1.3.304에서
256×256 ASTC 이미지를 처리했다. GPU RGBA 사본 262,144바이트와
word-plane 65,536개 값이 각각 기준값과 모두 일치했다. 같은 결과는
`hpc_fused_pipeline_v4` 데모로 재현할 수 있다.

패키지에 넣은 SPIR-V 자체도 `verify_device.ps1`로 다시 올려 실행했다.
한 dispatch와 fence가 완료됐으며, 가져온 두 출력은 검증용 파일과 SHA-256이
모두 일치했다. 실행 출력은 `verify/device_run.log`에 있다.

이 데모는 단일 기기·단일 ASTC 형식·단일 연산에서 검증됐다.
실행 속도 개선이나 다른 모델/기기의 호환성은 측정하지 않았다.

## 다시 빌드·검증

Windows PowerShell, Android SDK의 `adb`, Android NDK 30.0.15729638,
USB 디버깅이 허용된 Android 기기가 필요하다. 이 디렉터리에서 실행한다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\verify_device.ps1
```

다른 NDK 위치를 쓰면 `-NdkRoot C:\path\to\ndk`를 지정한다. 스크립트는
검증 바이너리를 빌드하고 SPIR-V·ASTC 입력을 기기에 올려 실행한 뒤,
RGBA와 word-plane 결과의 SHA-256을 기준값과 비교한다.
셰이더를 수정했다면 다음 명령으로 SPIR-V를 다시 만든다.

```powershell
glslang -V --target-env vulkan1.3 -o fused_astc_u64.spv fused_astc_u64.comp
```

## 라이선스와 구현 범위

이 디렉터리의 소스, 문서, 생성한 SPIR-V 및 테스트 에셋은
[Apache License 2.0](LICENSE)으로 공개한다. 저장소 루트에도 같은 라이선스가 있다.
ROCm의 작업 제출·완료
확인 흐름을 참고해 독립적으로 작성한 Vulkan 구현이며, AMD ROCm/HIP/ROCr
소스나 라이브러리를 포함하지 않는다. `verify/`의 C++·SVE 코드는 결과 검증용이고
GPU 커널의 실행 경로에는 들어가지 않는다. 빌드에 쓰는 Android NDK와 기기의
Vulkan 드라이버는 이 저장소에 배포되지 않으며 각자의 라이선스를 따른다.
