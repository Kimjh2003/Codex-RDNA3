# 커널 파일 안내

`source/`에는 수정할 수 있는 Slang 소스 5개, `compiled/`에는 실기기 검증에 쓴
SPIR-V 1.6 바이너리와 읽기용 어셈블리가 있다. 같은 파일 이름의 `.slang`과
`.spv`가 한 쌍이다.

바로 실행할 때는 컴파일할 필요 없이 `compiled/`의 SPIR-V를 사용한다.
소스를 수정한 뒤에는 저장소 루트에서 `scripts/compile_all.ps1`을 실행하고,
새 출력이 의도대로 만들어졌는지 확인한다. 그 명령의 기본 출력 위치는
`build/`이며 저장소에 자동 반영되지 않는다. `compiled/SHA256SUMS.txt`의
경로는 저장소 루트 기준이다.

[처음 실행하는 방법](../README.md) · [API와 레이아웃](../docs/reference/api-and-layouts.md)
