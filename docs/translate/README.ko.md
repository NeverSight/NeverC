**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← 문서](../README.ko.md)

# C++를 NeverC로 변환

실험적인 `neverc translate`는 `cpp-core-v1`, `cpp-project-v1`, `cpp-math-v1`으로 검토 가능한 `.nc` 소스를 생성합니다.

**현재 입력 언어로 C++만 구현되어 있습니다.** E Language(易语言, `.e`), Python, Go, Rust, TypeScript, JavaScript 지원은 향후 계획이며, 해당 언어의 변환기는 아직 제공되지 않습니다.

## 설치와 스칼라 변환

[프런트엔드 안내](../../utils/translate-frontends/cpp/README.md)에 따라 고정 버전 Clang 20.1.8 보조 프로그램을 빌드하거나 설치하세요. NeverC 옆에 두거나 `NEVERC_CPP_FRONTEND` 또는 `--frontend PATH`로 지정합니다. 변환에는 필요하지만 생성된 스칼라／프로젝트 출력의 컴파일에는 필요하지 않습니다.

```sh
neverc translate --from cpp input.cpp -o output.nc
neverc output.nc -c -o output.o
```

`cpp-core-v1`은 include가 없는 독립적인 C++17 소스 하나를 받습니다. `int`, `unsigned int`, `bool`, `void`, 단순 집합체 타입, 비멤버 함수, 네임스페이스, 오버로드와 문서화된 제어 흐름을 지원합니다. 사용하지 않는 코드를 포함하여 입력에 포함된 모든 선언을 검사합니다.

## 여러 파일로 구성된 프로젝트

컴파일 데이터베이스에서 번역 단위를 명시적으로 선택하고 프로젝트 루트 디렉터리를 지정합니다. 보조 프로그램은 각 단위를 따로 분석하며 병합기는 정의의 완전성, 링크 속성과 공유 타입을 검사하고 단일 정의 규칙(ODR) 준수 여부를 보수적으로 검증합니다.

```sh
neverc translate --from cpp --profile cpp-project-v1 \
  --project-root "$PWD" --compdb build/compile_commands.json \
  src/a.cpp src/b.cpp --out-dir generated
```

프로젝트 출력은 `translated.nc`와 `translated.h`입니다. 해당 루트 안의 프로젝트 헤더만 허용됩니다. 한 파일에 구성이 여러 개면 `--compdb-entry src/a.cpp=4`로 데이터베이스의 0부터 시작하는 인덱스를 선택합니다. 저장된 컴파일 명령은 데이터로만 분석하며 실행하지 않습니다.

## 제한된 배정밀도 수학 지원

`cpp-math-v1`은 프로젝트에 `double`, 문서에 명시된 변환과 비교, 정확한 시그니처의 `std::fabs(double)` 및 `std::floor(double)`을 추가합니다. 고정된 Clang 20.1.8 / libc++ 200100 / macOS SDK 15.5, SDK 설명 파일, 명시적인 macOS 15.0 대상(arm64 또는 x86_64)이 필요합니다. 일반적인 부동소수점 산술은 지원하지 않습니다. 예외 트랩은 마스킹하고 비정규수를 0으로 만드는 모드는 꺼야 합니다. 네 가지 표준 반올림 모드를 테스트했습니다.

```sh
neverc translate --from cpp --profile cpp-math-v1 \
  --target arm64-apple-macosx15.0.0 --cpp-sdk /path/to/neverc-cpp-sdk.json \
  --project-root "$PWD" --compdb build/compile_commands.json \
  src/math.cpp --out-dir generated-math
```

수학 변환은 설치된 NeverC 수학 헤더, 내장 구현의 식별 정보와 실제 링크도 검증합니다. 생성된 수학 모듈은 NeverC 런타임을 사용하며 C++ 도우미나 SDK가 필요하지 않습니다. `-fno-builtin-std`를 지정하면 최종 출력에 `fabs`/`floor` 매핑이 필요한 경우에만 파일을 쓰기 전에 변환이 실패합니다. 이 매핑이 필요 없는 수학 코드는 계속 변환할 수 있습니다.

## 검증과 출력

출력 옵션 대신 `--check`를 사용하면 같은 분석, 생성, 구문 및 오브젝트 검증을 수행하고 생성 파일을 남기지 않습니다. `--report PATH`는 구조화된 진단을 기록합니다. 변환 과정은 원본 프로그램을 실행하지 않습니다.

출력과 부속 파일은 덮어쓰지 않습니다. `--out-dir`는 기존 부모 디렉터리 아래 새 디렉터리를, `-o`는 새 `.nc` 경로를 요구합니다. 매니페스트는 대상 요구 사항, 입출력 해시와 컴파일 절차를 기록하며 소스 맵은 생성된 행을 원래 위치에 연결합니다.

실행은 네이티브 macOS arm64와 Rosetta에서 구동하는 macOS x86_64에서 검증했습니다. Intel Mac의 네이티브 실행, Linux, Windows 및 다른 대상을 지원한다는 의미는 아닙니다. 전체 C++／STL, 포인터, 참조, 배열, 예외, 템플릿, 문자열과 `std::vector`는 공개된 지원 범위에 포함되지 않습니다. [지원 표](../../utils/translate-frontends/docs/support-matrix.md), [프로토콜 및 복구 규칙](../../utils/translate-frontends/docs/protocol.md), [프로젝트 예제](../../tests/neverc/Inputs/translate/cpp/project/)를 참고하세요.
