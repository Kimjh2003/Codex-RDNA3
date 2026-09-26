> 해당코드는 Codex로 수정됨

# Android Studio 툴체인 교차 빌드

검증일: 2026-09-27 (Asia/Seoul)

- Android SDK NDK `30.0.15729638`: Clang 21.0.0, Android API 29, `arm64-v8a`
- Android SDK CMake `4.1.2`와 Ninja
- 공식 Khronos Vulkan-Headers 태그 `v1.4.344`
- NDK의 `libvulkan.so` 링크용 스텁

NDK에 포함된 Vulkan 헤더의 `VK_HEADER_VERSION`은 335라서, 저장소가 요구하는
344 헤더 검사를 통과하지 못했다. 위 태그의 헤더를 `Vulkan_INCLUDE_DIR`로
지정하고 `scripts/build_android_arm64.ps1`로 CMake 설정과 빌드를 실행했다.

결과: `librdna3_micro_engine.a` 생성 성공. `llvm-readelf`에서 아카이브 내부
오브젝트가 **ELF64 / AArch64**임을 확인했다. 헤더의 Vulkan API 버전 검사는
`1.4.344`로 통과했다. Clang의 나머지 경고는 Vulkan 구조체의 생략된 필드가
0으로 초기화되는 aggregate 초기화에 관한 것이다.

이 검증은 컴파일·정적 아카이브 생성까지다. RDNA3 실장 GPU에서 라이브러리를
사용한 Vulkan 1.4 제출과 커널 실행은 이 기록의 범위에 들어가지 않는다.
