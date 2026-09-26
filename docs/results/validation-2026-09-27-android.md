> 해당코드는 Codex로 수정됨

# Android Studio 툴체인 교차 빌드

검증일: 2026-09-27 (Asia/Seoul)

- Android SDK NDK `30.0.15729638`: Clang 21.0.0, Android API 33, `arm64-v8a`
- Android SDK CMake `4.1.2`와 Ninja
- NDK 내장 Vulkan 헤더 revision **335**, 최소 요구치 **304** 충족
- NDK API 33의 `libvulkan.so` 링크용 스텁

저장소의 CMake 요구 버전은 Vulkan **1.3.304 이상**이다. Android NDK의 기본
헤더만 사용해 `scripts/build_android_arm64.ps1`로 CMake 설정과 빌드를
실행했다. 별도 Vulkan-Headers 체크아웃은 필요하지 않았다.

결과: `librdna3_micro_engine.a` 생성 성공. `llvm-readelf`에서 아카이브 내부
오브젝트가 **ELF64 / AArch64**임을 확인했다. `VkPhysicalDeviceFeatures2`
기반 기능 체인과 `vkCmdPipelineBarrier2`를 Vulkan 1.3 기준으로 컴파일했다.

이 문서는 교차 빌드 기록이다. 같은 라이브러리의 Xclipse 940 실기기
**10개 GPU dispatch** 결과는
[GPU 실행 검증](validation-2026-09-27-xclipse-rdna3-gpu.md)에 있다.
