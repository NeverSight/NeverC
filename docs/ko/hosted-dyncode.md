**Languages**: [English](../hosted-dyncode.md) | [简体中文](../zh-CN/hosted-dyncode.md) | [繁體中文](../zh-TW/hosted-dyncode.md) | [日本語](../ja/hosted-dyncode.md) | [한국어](hosted-dyncode.md) | [Français](../fr/hosted-dyncode.md) | [Deutsch](../de/hosted-dyncode.md) | [Español](../es/hosted-dyncode.md) | [Italiano](../it/hosted-dyncode.md) | [Русский](../ru/hosted-dyncode.md) | [العربية](../ar/hosted-dyncode.md)

[← DynCode](dyncode-compiler.md)

# Hosted DynCode와 C++ 변환

기존 프로그램에 코드만 있는 함수를 설치할 때 `-fdyncode-hosted`를 사용합니다. `-fdyncode-keep-obj`가 필수이며 객체에 컴파일러 CFI/SEH를 보존하고 평면 페이로드에서는 제외합니다. 단일 코드 섹션과 오프셋 0의 진입점이 언와인드 기록과의 관계를 보존합니다.

암시적 시스템 호출, PEB, 힙 arena 및 문자열 런타임 변환을 끕니다. 쓰기 가능/TLS 전역과 미해결 호출은 실패합니다. 데이터나 콜백 포인터는 인수로 전달할 수 있습니다. 독립 DynCode 동작은 유지됩니다.

코어 C++ 변환은 `--target`을 받고 C23, 소스 맵, 매니페스트를 대상 ABI에 연결합니다. `cpp-core-v2`는 템플릿과 정상 흐름의 생성·소멸을 포함한 문서화된 C++17 부분집합을 지원합니다. C++ 예외, RTTI, 가상 디스패치와 외부 예외 전파 중 정리는 지원하지 않습니다.

```sh
neverc translate --from cpp replacement.cpp --profile cpp-core-v2 \
  --target aarch64-unknown-linux-gnu -o replacement.nc -- -std=c++17
neverc --no-default-config --target=aarch64-unknown-linux-gnu \
  -fdyncode -fdyncode-hosted -fdyncode-entry=replacement \
  -fdyncode-keep-obj=replacement.o -fdyncode-report=replacement.audit.json \
  -o replacement.bin replacement.nc
```

감사에는 `execution_profile="hosted-v1"`, `keep_unwind=true`, `target_triple`이 포함됩니다. `input_digest`는 보존 객체, `output_digest`는 재배치와 검증이 끝난 페이로드의 해시입니다. 설치기는 두 해시, 최종 코드와 진입점 배치를 검증한 뒤 실제 언와인드 기록을 등록해야 합니다.
