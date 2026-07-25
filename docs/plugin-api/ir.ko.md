**언어**: [English](ir.md) | [简体中文](ir.zh-CN.md) | [繁體中文](ir.zh-TW.md) | [日本語](ir.ja.md) | [한국어](ir.ko.md) | [Français](ir.fr.md) | [Deutsch](ir.de.md) | [Español](ir.es.md) | [Italiano](ir.it.md) | [Русский](ir.ru.md) | [العربية](ir.ar.md)

# NeverC 플러그인 IR API

첫 공개 플러그인 ABI는 안정적인 C 테이블을 통해 LLVM IR을 노출합니다. 플러그인은
LLVM 헤더를 포함하지 않으며, NeverC 핸들을 LLVM 객체로 캐스팅해서는 안 됩니다.

## 인터페이스

`neverc_plugin_entry` 안에서 `NevercBootstrapAPI.QueryInterface`로 인터페이스를
질의하십시오.

- `NEVERC_INTERFACE_IR_CORE` — 모듈, 타입, 값, CFG, 메타데이터, 어트리뷰트, 상수,
  직렬화 질의.
- `NEVERC_INTERFACE_IR_BUILDER` — 트랜잭션 방식의 IR 구성과 변경.
- `NEVERC_INTERFACE_IR_ANALYSIS` — 내장 분석과 플러그인 정의 분석.
- `NEVERC_INTERFACE_IR_PASS` — Module, CGSCC, Function, Loop 패스.
- `NEVERC_INTERFACE_IR_GEN` — SemanticUnit에서 IR로의 하강 과정 교체.
- `NEVERC_INTERFACE_IR_OPTIMIZATION` — 최적화 파이프라인 전체 교체.

항상 헤더의 major/minor 쌍을 요청하고, 반환된 `StructSize`가 플러그인이 사용하는
마지막 함수 포인터까지 닿는지 확인하십시오. 더 새로운 호스트는 필드를 덧붙일 수
있으므로 플러그인은 알 수 없는 뒷부분을 무시해야 합니다.

## 핸들과 소유권

IR 핸들은 태스크로 범위가 한정된 불투명한 `{Owner, Value}` 쌍입니다. 그것들이
참조하는 모든 객체는 호스트가 소유합니다.

- 태스크 범위 핸들을 콜백이나 태스크가 끝난 뒤에 보유하지 마십시오.
- 핸들을 다른 세션이나 태스크에서 사용하지 마십시오.
- 커밋된 교체는 교체된 객체의 핸들을 무효화합니다.
- 중단된 변경은 그 변경이 만든 핸들을 만료시킵니다.
- API는 LLVM 포인터를 노출하는 대신 `NEVERC_STATUS_STALE_HANDLE`, `WRONG_OWNER`,
  `WRONG_TYPE`을 보고합니다.

질의 호출이 반환하는 문자열과 바이트 뷰는, API가 해제 가능한 버퍼를 반환한다고 명시
하지 않는 한 빌려온 것입니다.

## IR 읽기

`NevercIRCoreAPI`가 제공하는 것은 다음과 같습니다.

- 모듈 식별자, 트리플, 데이터 레이아웃, 인라인 어셈블리;
- 함수, 전역, 블록, 명령어, use, 오퍼랜드에 대한 안정적인 값 커서;
- 안정적인 타입 ID와 명령코드 ID;
- 함수, 전역, 명령어, 메타데이터, 어트리뷰트 속성;
- 정수, 부동소수점, 집합체, null, poison, undef 상수;
- 비트코드 내보내기/가져오기와 검증된 모듈 산출물.

컬렉션 커서에는 한도가 있습니다. 출력 용량을 전달하고 반환된 개수가 0이 될 때까지
수집을 반복하십시오.

## 트랜잭션 변경

모든 구조 변경은 `NevercIRBuilderAPI`를 사용합니다.

1. 모듈 또는 함수 변경을 시작합니다.
2. 그 변경에 묶인 빌더를 만듭니다.
3. 삽입 지점을 정하고 명령어, 함수, 블록을 만듭니다.
4. 변경을 커밋합니다.
5. 빌더와 변경 핸들을 파괴합니다.

커밋은 후보 IR을 검증하고 원자적으로 게시합니다. 검증기가 실패하면 호스트는 변경을
롤백하고 이전 모듈을 유지합니다. `AbortMutation`은 항상 스테이징된 변경을
되돌립니다.

IR을 바꾼 뒤에 `NEVERC_IR_PRESERVE_ALL`을 주장하지 마십시오. 패스 어댑터는 모듈
세대를 확인하고 일관되지 않은 보존 선언을 거부합니다.

## 패스 수준과 단계

`NevercIRPassDescriptor.Level`이 지원하는 것은 다음과 같습니다.

- `NEVERC_IR_PASS_LEVEL_MODULE`
- `NEVERC_IR_PASS_LEVEL_CGSCC`
- `NEVERC_IR_PASS_LEVEL_FUNCTION`
- `NEVERC_IR_PASS_LEVEL_LOOP`

안정적인 삽입 단계는 `PRE_OPT`, `PIPELINE_START`, `OPTIMIZER_LAST`, `POST_OPT`,
`PRE_CODEGEN`입니다. 호출에는 해당 수준에서 유효한 핸들만 담깁니다. 함수 패스와 루프
패스는 동시에 실행될 수 있으므로, 가변 플러그인 상태는 선언한 동시성 계약을 따라야
합니다.

호스트는 항상 마지막 봉인된 IR 검증기를 실행합니다. 플러그인은 그 게이트를 교체하거나
가로채거나 건너뛸 수 없습니다.

## 분석

내장 분석 ID는 호출 그래프, 지배자 트리, 후지배자 트리, 루프 정보, 스칼라 전개,
MemorySSA, 별칭 분석을 다룹니다.

플러그인 분석은 의존성과 수명 주기 콜백을 선언합니다. 결과는 호출마다 캐시되며 패스의
보존 결과에 따라 무효화됩니다. 재귀적 의존 순환과 분석 콜백에서의 변경은 거부됩니다.

## 완전한 프로바이더

IR 생성 프로바이더는 내장 하강 과정을 교체하고 검증된 모듈 산출물을 게시할 수
있습니다. 최적화 프로바이더는 내장 최적화 파이프라인 전체를 교체할 수 있습니다. 두
경로 모두 다음을 지킵니다.

- 명시적인 단계 입력을 소비합니다;
- LLVM 포인터를 반환하는 대신 호스트 API를 통해 게시합니다;
- 타깃 호환성과 모듈 유효성을 검증합니다;
- 게시가 실패하면 이전 모듈을 원자적으로 유지합니다.

최적화 프로바이더 뒤에도 최종 검증기는 여전히 필수입니다.

## 최소 예제

`pluginsdk/examples/FunctionPass.c`는 읽기 전용 함수 패스입니다.
`pluginsdk/examples/ExamplePlugin.c`는 모듈 열거를 보여 주고,
`pluginsdk/examples/CustomCallConvPlugin.c`는 어트리뷰트와 호출 지점 속성을
시연합니다.

예제를 빌드하고 로드하기:

```sh
cmake --build build-neverc --target neverc-plugin-example-function-pass
build-neverc/bin/neverc \
  -fplugin=build-neverc/neverc/pluginsdk/examples/host/FunctionPass.so \
  -O2 -c input.c -o input.o
```

CMake가 생성하는 플랫폼별 모듈 확장자를 사용하십시오.

## 실패 규칙

모든 콜백에서 `NevercStatus`를 반환하십시오. 플러그인 실패는 구조화된 진단이 됩니다.
예외를 C 경계 너머로 던지지 마십시오. 모든 출력 테이블 헤더와 예약 필드를 초기화하고,
필수 포인터가 없으면 `INVALID_ARGUMENT`를 반환하십시오.

규범적 ABI 선언, 단계 정책, 테스트 증거는 `PluginIR.h`, `PluginPhaseSchema.h`,
`coverage.json`을 참조하십시오.
