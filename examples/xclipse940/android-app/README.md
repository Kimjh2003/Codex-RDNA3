# Xclipse 940 Android 디버그 앱

`verify/hpc_fused_demo.cpp`의 동일한 Vulkan 1.3 compute 경로를 앱 프로세스에서
실행하는 얇은 래퍼다. 실행 후 앱 화면과 `XclipseGpuDemo` logcat 태그에
ASTC RGBA 및 word-plane SHA-256 검증 결과를 표시한다. 시작 5초 뒤 자동
실행하며 버튼으로 다시 실행할 수도 있다.

Windows PowerShell, Android Studio JBR, Android SDK platform 36 / build-tools
36.0.0, NDK 30.0.15729638이 필요하다. 이 디렉터리에서 실행한다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build.ps1
adb install -r .\build\xclipse-gpu-demo-debug.apk
adb shell am start -n dev.kimjh.xclipsegpu/.MainActivity
adb logcat -d -b main | Select-String XclipseGpuDemo
```

`build.ps1`은 프로젝트 안의 SPIR-V와 검증 입력을 APK asset으로 넣고
arm64 JNI 라이브러리를 NDK로 빌드한다. 디버그 키는 `build/` 안에 생성되며
저장소에 포함되지 않는다. 앱은 `android:debuggable=true`여서 Sokatoa의
GFXR 캡처 대상으로 선택할 수 있다.

Sokatoa에서는 `Launch` 모드, 패키지 `dev.kimjh.xclipsegpu`, 시작 `Delay since
app launch` 500 ms, 종료 `Duration since capture start` 12000 ms,
GFXR 활성화를 사용한다. 이 앱의 Vulkan 경로는 compute 전용이므로 화면
프레임을 `vkQueuePresentKHR`로 제출하지 않는다. GFXR이 파일 종료 또는
성능 데이터 불완전 경고를 낼 수 있지만, 캡처된 트레이스에서
`Queue Submit → Command Buffer → vkCmdDispatch`를 확인했다. 성능 수치를
이 데모의 벤치마크로 사용하지 않는다.
