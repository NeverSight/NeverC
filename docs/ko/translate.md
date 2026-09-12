**언어**: [English](../translate.md) | [简体中文](../zh-CN/translate.md) | [繁體中文](../zh-TW/translate.md) | [日本語](../ja/translate.md) | [한국어](translate.md) | [Français](../fr/translate.md) | [Deutsch](../de/translate.md) | [Español](../es/translate.md) | [Italiano](../it/translate.md) | [Русский](../ru/translate.md) | [العربية](../ar/translate.md)

[← 문서](README.md)

# C++를 NeverC로 변환

실험적인 `neverc translate`는 `cpp-core-v1`, `cpp-core-v2`, `cpp-project-v1`, `cpp-math-v1`으로 검토 가능한 `.nc` 소스를 생성합니다.

**현재 입력 언어로 C++만 구현되어 있습니다.** E Language(易语言, `.e`), Python, Go, Rust, TypeScript, JavaScript 지원은 향후 계획이며, 해당 언어의 변환기는 아직 제공되지 않습니다.

## 설치와 스칼라 변환

일반 NeverC 설치와 표준 리소스를 사용하면 됩니다. C++ 프런트엔드와 승인된 SDK 헤더가 내장되어 있어 Clang을 별도로 설치할 필요가 없습니다. 빌드 세부 사항은 [프런트엔드 안내](../../utils/translate-frontends/cpp/README.md)를 참고하세요.

```sh
neverc translate --from cpp input.cpp -o output.nc
neverc output.nc -c -o output.o
```

`cpp-core-v1`은 include가 없는 독립적인 C++17 소스 하나를 받습니다. `int`, `unsigned int`, `bool`, `void`, 단순 집합체 타입, 비멤버 함수, 네임스페이스, 오버로드와 문서화된 제어 흐름을 지원합니다. 사용하지 않는 코드를 포함하여 입력에 포함된 모든 선언을 검사합니다.

`--profile cpp-core-v2`를 선택하면 검증된 `typedef`/`using` 타입 별칭, 지원하는 정수 타입을 기반으로 하는 열거형, `static_assert`, 제한된 객체 포인터와 좌측값 참조를 추가로 변환할 수 있습니다. 참조 매개변수와 반환 참조는 원래 객체를 가리키며, 다중 포인터의 `const`와 널 포인터를 지원합니다. 단일 소스와 include 금지 제한은 유지됩니다. [core v2 지원 범위](../../utils/translate-frontends/docs/cpp-core-v2.md)를 참고하세요.

core v2는 고정 길이 지역 배열과 배열 필드, 다차원 인덱싱, 배열 포인터와 참조도 지원합니다. 부분 초기화는 생략된 스칼라 요소를 0으로 초기화하고 레코드 요소에는 선택된 초기화를 적용하며 요소 초기화 순서와 별칭 관계를 유지합니다. 배열 길이와 초기화 확장량에는 상한이 있습니다. 전역 배열과 가변 길이 배열은 아직 지원하지 않습니다.

core v2는 C++17 초기화 문, 분기 이어 실행 및 검증된 `[[fallthrough]]` 표기를 포함한 `switch`/`case`/`default`를 지원합니다. 선택식은 한 번만 평가하며 중첩 switch와 루프의 `break`/`continue` 대상이 유지됩니다. GNU case 범위와 다른 문 속성은 허용하지 않습니다.

core v2는 부호 있는 8／16／32／64비트 정수와 부호 없는 정수, 문자 타입과 리터럴, 상수 `sizeof`／`alignof` 질의를 지원합니다. 소스의 정수 승격과 오버로드 결정을 먼저 수행한 뒤 비트 폭을 정규화하며 `long`, `wchar_t`, 크기 타입은 타깃을 따릅니다. 열거형의 기반 타입에도 작은 정수와 큰 정수를 사용할 수 있습니다. 런타임 문자열과 STL은 개발 중입니다.

core v2는 객체 포인터의 오프셋, 차이, 증감 및 복합 대입을 지원하며 배열 순회와 다차원 배열의 간격을 유지합니다. 생성된 보조 함수는 C++17에서 널 포인터에 0을 더하거나 빼는 동작과 널 포인터끼리의 차이를 보존합니다. 포인터 대소 비교, 포인터와 정수 간 변환, STL 컨테이너와 알고리즘은 아직 지원하지 않습니다.

core v2는 소스 타입의 크기와 ABI 정렬을 NeverC 자체 타깃 모델과 대조하며, 구조체 크기와 필드 오프셋도 검증합니다. 생성 코드의 컴파일 시점 단언으로 메모리 배치를 다시 확인하고 해당 정보를 매니페스트에 기록합니다.

Core v2는 지원되는 레코드 타입의 이름 있는 비가상 멤버 함수를 지원합니다. const 오버로드, lvalue 한정 메서드, 정적 메서드 및 `this`를 포함합니다. 호출은 원래 객체의 동일성을 유지하며 인수보다 먼저 수신 객체를 평가합니다. 임시 객체의 비정적 호출, 상속, 템플릿 및 전체 STL은 아직 지원하지 않습니다.

Core v2는 표준 레이아웃과 지원되는 복사 연산을 갖는 레코드의 일반 사용자 정의 생성자도 지원합니다. 지역 객체, 필드와 배열 요소는 최종 저장 위치에 직접 생성되며 필드는 선언 순서대로 초기화됩니다. 명시적 default 생성자, 위임 생성자, 이동 생성자와 예외는 아직 지원하지 않습니다.

Core v2는 값으로 전달되는 각 레코드 매개변수에 독립된 객체를 만들고 반환값을 호출자의 저장소에 직접 기록합니다. 생성자와 메서드에도 같은 규칙을 적용하며 필요한 복사와 참조의 별칭 관계를 유지합니다. 조건을 만족하는 자명한 타입에서는 다른 C++17 구현이 인수나 반환값을 추가로 복사할 수 있습니다.

Core v2는 일반 사용자 정의 소멸자와 정상 종료 시의 암시적 멤버 소멸을 지원합니다. 지역 객체, 필드와 배열 요소는 역순으로 소멸하며, 임시 객체는 필요한 값을 저장한 뒤 전체 표현식이 끝날 때 정리합니다. 반환, 분기, 반복문, break와 continue에서 필요한 정리를 수행합니다. 값 매개변수는 피호출 함수가 종료될 때 소멸하고 반환 객체는 호출자가 관리합니다. 명시적 소멸자 호출, 직접 작성한 예외 명세, 정적 객체 소멸과 예외 스택 풀기는 아직 지원하지 않습니다. 암시적 비자명 복사, 이동 연산과 전체 STL 지원은 개발 중입니다.

Core v2는 원본 매개변수가 `R&` 또는 `const R&`인 일반 사용자 정의 복사 생성자와 복사 대입 연산자를 지원합니다. 실제 대상에서 복사하며 부수 효과와 반환 참조를 유지합니다. 대입 연산자 구문은 오른쪽 피연산자를 먼저 평가하고, 명시적 `operator=` 호출은 수신 객체를 먼저 평가합니다. default 특수 멤버와 암시적 비자명 복사는 아직 지원하지 않습니다.

## 여러 파일로 구성된 프로젝트

컴파일 데이터베이스에서 번역 단위를 명시적으로 선택하고 프로젝트 루트 디렉터리를 지정합니다. 내장 프런트엔드는 각 단위를 따로 분석하며 병합기는 정의의 완전성, 링크 속성과 공유 타입을 검사하고 단일 정의 규칙(ODR) 준수 여부를 보수적으로 검증합니다.

```sh
neverc translate --from cpp --profile cpp-project-v1 \
  --project-root "$PWD" --compdb build/compile_commands.json \
  src/a.cpp src/b.cpp --out-dir generated
```

프로젝트 출력은 `translated.nc`와 `translated.h`입니다. 해당 루트 안의 프로젝트 헤더만 허용됩니다. 한 파일에 구성이 여러 개면 `--compdb-entry src/a.cpp=4`로 데이터베이스의 0부터 시작하는 인덱스를 선택합니다. 저장된 컴파일 명령은 데이터로만 분석하며 실행하지 않습니다.

## 제한된 배정밀도 수학 지원

`cpp-math-v1`은 프로젝트에 `double`, 문서에 명시된 변환과 비교, 정확한 시그니처의 `std::fabs(double)` 및 `std::floor(double)`을 추가합니다. 내장된 Clang 20.1.8 / libc++ 200100 / macOS 15.5 헤더 모음을 사용하며 macOS 15.0 대상(arm64 또는 x86_64)을 명시해야 합니다. 일반적인 부동소수점 산술은 지원하지 않습니다. 예외 트랩은 마스킹하고 비정규수를 0으로 만드는 모드는 꺼야 합니다. 네 가지 표준 반올림 모드를 테스트했습니다.

```sh
neverc translate --from cpp --profile cpp-math-v1 \
  --target arm64-apple-macosx15.0.0 \
  --project-root "$PWD" --compdb build/compile_commands.json \
  src/math.cpp --out-dir generated-math
```

수학 변환은 설치된 NeverC 수학 헤더, 내장 구현의 식별 정보와 실제 링크도 검증합니다. 생성된 수학 모듈은 NeverC 런타임을 사용합니다. `-fno-builtin-std`를 지정하면 최종 출력에 `fabs`/`floor` 매핑이 필요한 경우에만 파일을 쓰기 전에 변환이 실패합니다. 이 매핑이 필요 없는 수학 코드는 계속 변환할 수 있습니다.

## 검증과 출력

출력 옵션 대신 `--check`를 사용하면 같은 분석, 생성, 구문 및 오브젝트 검증을 수행하고 생성 파일을 남기지 않습니다. `--report PATH`는 구조화된 진단을 기록합니다. 변환 과정은 원본 프로그램을 실행하지 않습니다.

출력과 부속 파일은 덮어쓰지 않습니다. `--out-dir`는 기존 부모 디렉터리 아래 새 디렉터리를, `-o`는 새 `.nc` 경로를 요구합니다. 매니페스트는 대상 요구 사항, 입출력 해시와 컴파일 절차를 기록하며 소스 맵은 생성된 행을 원래 위치에 연결합니다.

CI 결과, 실행 및 설치 검증, 건너뛴 테스트는 플랫폼별로 기록합니다. 네이티브 macOS arm64와 Rosetta에서 구동하는 macOS x86_64는 서로 다른 검증 환경으로 구분합니다. 전체 C++／STL, 예외, 템플릿, 문자열과 `std::vector`는 공개된 지원 범위에 포함되지 않습니다. [지원 표](../../utils/translate-frontends/docs/support-matrix.md), [프로토콜 및 복구 규칙](../../utils/translate-frontends/docs/protocol.md), [프로젝트 예제](../../tests/neverc/Inputs/translate/cpp/project)를 참고하세요.
