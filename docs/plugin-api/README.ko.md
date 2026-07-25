**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# NeverC 플러그인 ABI

NeverC의 첫 공개 플러그인 ABI는 순수 C 기반의 페이즈(phase) 인터페이스입니다.
플러그인은 함수를 하나만 내보내는 공유 모듈이며, 버전이 매겨진 기능 테이블을
협상하고 명시적인 Process / Session / Task 스코프 안에서 실행됩니다. LLVM 헤더를
포함하지 않고, 컴파일러를 링크하지 않으며, 경계를 넘어 C++ 타입을 주고받지도
않습니다.

미출시 프로토타입 API와 그 `nevercGetPluginInfo` 진입점은 **제거되었습니다**.
프로토타입 바이너리는 마이그레이션 진단과 함께 거부되므로, 공개 헤더에 맞추어
소스를 다시 컴파일하십시오. 구 API에서 신 API로의 전체 대응표는
[프로토타입 API에서 마이그레이션](migration-from-prototype.ko.md)을 참고하세요.

## 여기서 시작하기

- [Source 및 I/O API](source.ko.md)
- [전처리기 API](prep.ko.md)
- [AST 및 의미 분석 API](ast-sema.ko.md)
- [IR API](ir.ko.md)
- [MIR API](mir.ko.md)
- [Target / MC / 어셈블리 / 오브젝트 API](target-mc-object.ko.md)
- [DynCode API](dyncode.ko.md)
- [사용자 정의 호출 규약](custom-callconv/README.ko.md)
- [프로토타입 API에서 마이그레이션](migration-from-prototype.ko.md)
- [페이즈 커버리지 증거](coverage.json)

## 실행 모델

호스트는 세 겹으로 중첩된 스코프를 통해 플러그인을 구동합니다. 각 스코프는
플러그인에 불투명한 상태 포인터를 넘겨주며, 그 할당과 소유는 플러그인이 직접
담당합니다. 따라서 올바르게 작성된 플러그인에는 전역 가변 상태가 필요 없습니다.

| 스코프 | 콜백 | 의미 |
|---|---|---|
| Process | `ProcessBegin`, `Register`, `Destroy` | 컴파일러 프로세스 하나. 여기서 인터페이스를 조회하고 기능을 등록합니다. |
| Session | `SessionBegin`, `SessionEnd` | 드라이버 호출 한 번. |
| Task | `TaskBegin`, `TaskEnd` | 작업 단위 하나. `NevercTaskKind`로 식별됩니다. |

Task 종류는 `INVOCATION`, `TRANSLATION_UNIT`, `LTO`, `LINK`, `CODEGEN`,
`OBJECT`, `DYNCODE`입니다.

호스트는 먼저 `ProcessBegin`을 호출하고, 이어서 `Register`를 정확히 한 번
호출합니다. 옵션, 옵저버, 인터셉터, Provider를 추가할 수 있는 곳은 등록 단계
뿐이며 그 이후 페이즈 그래프는 동결됩니다.

## 페이즈

페이즈는 입력 아티팩트에서 출력 아티팩트로 가는, 이름과 버전을 가진 전이입니다.
NeverC는 driver, source, 전처리기, 구문, 의미 분석, IR, codegen, MIR, MC,
어셈블리, 오브젝트, 링크, dyncode 도메인에 걸쳐 **130개의 내장 페이즈**를
제공하며, 플러그인 정의 페이즈를 위한 확장 ID 패밀리 8개를 예약하고 있습니다.

각 페이즈는 자신의 정책을 선언하며, 플러그인은 그 정책이 허용하는 방식으로만
연결할 수 있습니다.

| 정책 플래그 | 플러그인이 할 수 있는 것 |
|---|---|
| `NEVERC_PHASE_OBSERVABLE` | 옵저버를 등록해 읽기 전용으로 통지를 받습니다. |
| `NEVERC_PHASE_INTERCEPTABLE` | 페이즈를 감싸고 체인의 나머지를 호출할지 결정합니다. |
| `NEVERC_PHASE_REPLACEABLE` | Provider를 등록해 출력을 직접 공급합니다. |
| `NEVERC_PHASE_SKIPPABLE_WITH_PROOF` | proof 핸들을 제공하는 조건으로 전이를 건너뜁니다. |
| `NEVERC_PHASE_SEALED_HOST_GATE` | 아무것도 할 수 없습니다. 검증기와 커밋은 호스트 전용이며 대체·가로채기·건너뛰기가 불가능합니다. |

옵저버는 페이즈가 선언한 시점에 전달됩니다: `NEVERC_OBSERVER_BEFORE`,
`NEVERC_OBSERVER_AFTER`, `NEVERC_OBSERVER_AFTER_COMMIT`.

인터셉터는 `NevercPhaseContinuation`을 받습니다. `InvokeNext`는 콜백 스레드에서
**최대 한 번만** 호출해야 하며, 그 후 `NevercPhaseResult.Action`에
`NEVERC_PHASE_CONTINUE`, `NEVERC_PHASE_REPLACE`, `NEVERC_PHASE_SKIP` 중 하나를
보고해야 합니다.

페이즈 ID, 정책, 안정성 등급, 검증 게이트의 규범적 출처는
`neverc/include/neverc/Plugin/Schema/PhaseSchema.json`입니다. 생성된
`PluginPhaseSchema.inc`가 이를 `NEVERC_PHASE_IR_PASS_PRE_OPT_HIGH` / `_LOW`
같은 컴파일 타임 상수로 노출합니다.

## 완전한 최소 플러그인

다음은 `pluginsdk/templates/minimal/Plugin.c`입니다. 로드되고, ABI를 협상하고,
아무것도 등록하지 않으며, 깔끔하게 언로드됩니다. 이 디렉터리를 복사해서
확장해 나가십시오.

```c
#include "neverc/Plugin/NevercPluginAPI.h"

#define MINIMAL_PLUGIN_ID "com.example.minimal"
#define STRING_VIEW_LITERAL(Text)                                              \
  { (Text), (uint64_t)(sizeof(Text) - 1) }

static NevercStatus status_code(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

static void copy_bytes(void *Destination, const void *Source, uint64_t Count) {
  uint64_t Index;
  unsigned char *Out = (unsigned char *)Destination;
  const unsigned char *In = (const unsigned char *)Source;
  for (Index = 0; Index != Count; ++Index)
    Out[Index] = In[Index];
}

static NevercStatus NEVERC_CALL
process_begin(const NevercCoreAPI *Core, void **OutProcessState) {
  if (Core == NULL || OutProcessState == NULL)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutProcessState = NULL;
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL
register_plugin(const NevercCoreAPI *Core, const NevercRegistrarAPI *Registrar,
                void *RegistrarContext, void *ProcessState) {
  (void)Core;
  (void)RegistrarContext;
  (void)ProcessState;
  if (Registrar == NULL)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  /* 여기서 옵션, 옵저버, 인터셉터, Provider를 등록합니다. */
  return neverc_status_ok();
}

NEVERC_EXPORT NevercStatus NEVERC_CALL
neverc_plugin_entry(const NevercBootstrapAPI *Bootstrap,
                    NevercPluginDescriptor *OutPlugin) {
  NevercPluginDescriptor Descriptor = {0};
  uint32_t Capacity;
  uint64_t BytesToWrite;
  if (Bootstrap == NULL || OutPlugin == NULL ||
      OutPlugin->Header.StructSize < sizeof(uint32_t))
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  Capacity = OutPlugin->Header.StructSize;
  Descriptor.Header = (NevercABITableHeader){
      sizeof(Descriptor), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Descriptor.PluginID = (NevercStringView)STRING_VIEW_LITERAL(MINIMAL_PLUGIN_ID);
  Descriptor.DisplayName =
      (NevercStringView)STRING_VIEW_LITERAL("Minimal Plugin");
  Descriptor.Version = (NevercSemanticVersion){1, 0, 0, 0};
  Descriptor.Concurrency = NEVERC_CONCURRENCY_SESSION_SERIAL;
  Descriptor.Reentrancy = NEVERC_REENTRANCY_ALLOWED;
  Descriptor.ProcessBegin = process_begin;
  Descriptor.Register = register_plugin;
  BytesToWrite = Capacity < sizeof(Descriptor) ? Capacity : sizeof(Descriptor);
  copy_bytes(OutPlugin, &Descriptor, BytesToWrite);
  return neverc_status_ok();
}
```

`OutPlugin`은 호출자가 소유하는 버퍼입니다. 진입 시 `Header.StructSize`는 쓰기
가능한 용량을 나타냅니다. 플러그인은 그 용량을 넘지 않는 범위에서 기록하고,
실제로 생성한 크기를 보고합니다.

## 인터페이스 협상

기능 테이블은 심볼이 아니라 128비트 인터페이스 ID로 가져옵니다. 컴파일할 때
사용한 major와, 동작 가능한 최소 minor를 요청하십시오.

```c
const void *Table = NULL;
uint16_t Minor = 0;
uint64_t TableSize = 0;
NevercStatus Status = Bootstrap->QueryInterface(
    Bootstrap->Context,
    (NevercInterfaceID){NEVERC_INTERFACE_IR_PASS_HIGH,
                        NEVERC_INTERFACE_IR_PASS_LOW},
    NEVERC_IR_PASS_API_MAJOR, NEVERC_IR_PASS_API_MINOR, &Table, &Minor,
    &TableSize);
if (Status.Code != NEVERC_STATUS_OK)
  return Status;
if (!Table || TableSize < offsetof(NevercIRPassAPI, RegisterPass) +
                              sizeof(((NevercIRPassAPI *)0)->RegisterPass))
  return fail(NEVERC_STATUS_ABI_MISMATCH);
```

호출할 마지막 함수의 오프셋과 `TableSize`를 대조하는 것 — 이것이 이 ABI를
확장 가능하게 만드는 규칙입니다. 새 호스트는 필드를 끝에 덧붙이고, 오래된
플러그인은 자신이 검증한 접두부 너머를 결코 읽지 않으므로 계속 동작합니다.
전달받은 구조체에 동일한 검사를 적용하는 매크로가
`NEVERC_ABI_FIELD_AVAILABLE(header, type, field)`입니다.

공개 인터페이스와 해당 헤더:

| 인터페이스 | 테이블 | 헤더 |
|---|---|---|
| `NEVERC_INTERFACE_CORE` | `NevercCoreAPI` | `PluginCore.h` |
| `NEVERC_INTERFACE_DRIVER` | `NevercDriverAPI` | `PluginDriver.h` |
| `NEVERC_INTERFACE_IO`, `..._SOURCE_LOCATION` | `NevercIOAPI`, `NevercSourceLocationAPI` | `PluginSource.h` |
| `NEVERC_INTERFACE_PREP` | `NevercPrepAPI` | `PluginPrep.h` |
| `NEVERC_INTERFACE_AST`, `..._PARSER` | `NevercASTAPI`, `NevercParserAPI` | `PluginAST.h` |
| `NEVERC_INTERFACE_SEMA` | `NevercSemaAPI` | `PluginSema.h` |
| `NEVERC_INTERFACE_IR_CORE`, `..._BUILDER`, `..._ANALYSIS`, `..._PASS`, `..._GEN`, `..._OPTIMIZATION` | IR 테이블들 | `PluginIR.h` |
| `NEVERC_INTERFACE_TARGET`, `..._TARGET_ABI`, `..._CALLING_CONVENTION` | Target 테이블들 | `PluginTarget.h` |
| `NEVERC_INTERFACE_MIR`, `..._MIR_ANALYSIS`, `..._MIR_PASS`, `..._MIR_PROVIDER` | MIR 테이블들 | `PluginMIR.h` |
| `NEVERC_INTERFACE_MC`, `..._MC_EMISSION`, `..._MC_PROVIDER`, `..._ASSEMBLY_PROVIDER` | MC 테이블들 | `PluginMC.h` |
| `NEVERC_INTERFACE_OBJECT`, `..._OBJECT_FORMAT`, `..._OBJECT_PHASE` | Object 테이블들 | `PluginObject.h` |
| `NEVERC_INTERFACE_LINK`, `..._LINK_REGISTRAR`, `..._LINK_PHASE` | Link 테이블들 | `PluginLink.h` |
| `NEVERC_INTERFACE_LTO`, `..._LTO_REGISTRAR` | LTO 테이블들 | `PluginLTO.h` |
| `NEVERC_INTERFACE_DYNCODE`, `..._DYNCODE_REGISTRAR`, `..._DYNCODE_PHASE` | DynCode 테이블들 | `PluginDynCode.h` |

인터페이스는 STABLE(새 호스트는 추가만 가능) 또는 LOCKSTEP(타깃 고유 스키마로
정확히 일치해야 함) 중 하나입니다. LOCKSTEP 값을 사용하기 전에 스키마 다이제스트를
비교하십시오.

## 빌드

집합 헤더를 포함하거나, 사용하는 도메인만 포함합니다.

```c
#include "neverc/Plugin/NevercPluginAPI.h"
```

NeverC 자체로 공유 모듈 빌드하기:

```sh
neverc --target=arm64-apple-macosx -shared \
  -I/path/to/pluginsdk/include \
  -o MyPlugin.dylib MyPlugin.c
```

설치된 SDK에 대해 CMake로 빌드하기:

```cmake
find_package(NevercPluginSDK REQUIRED)
add_library(my_plugin MODULE my_plugin.c)
target_link_libraries(my_plugin PRIVATE NevercPluginSDK::headers)
```

pkg-config 사용하기:

```sh
cc -shared $(pkg-config --cflags neverc-plugin) -o my_plugin.so my_plugin.c
```

호스트에 맞게 `.so`, `.dylib`, `.dll`을 사용하십시오. SDK는 LLVM도 NeverC
런타임도 링크하지 않습니다. `NevercPluginSDK::headers`는 헤더 전용 타깃입니다.

## 로드와 설정

```sh
neverc -fplugin=./MyPlugin.dylib -c input.c -o input.o
```

| 옵션 | 형태 | 용도 |
|---|---|---|
| `-fplugin=<path>` | 반복 가능 | 플러그인 공유 모듈을 로드합니다. |
| `-fplugin-arg=<plugin-id>:<key>=<value>` | 반복 가능 | 등록된 플러그인 옵션에 네임스페이스가 붙은 값을 전달합니다. |
| `-fplugin-provider=<phase>:<plugin-id>` | 반복 가능 | 교체 가능한 페이즈를 어떤 플러그인이 제공할지 선택합니다. |

`<plugin-id>:` 한정자는 활성 플러그인이 정확히 하나일 때만 생략할 수 있습니다.
플러그인이 `RegisterOption`으로 등록한 옵션은 선언된 철자로 직접 지정할 수도
있으며 flag, joined, separate, 다중 인자 형태를 지원합니다. `-fplugin=` 없이
플러그인 인자나 Provider 선택을 주면 조용히 무시되는 것이 아니라 하드 에러가
됩니다.

## ABI 규칙

- 기능 테이블은 `QueryInterface`로 조회하고, major 일치를 요구하며, 필드에
  접근하기 전에 `StructSize`를 검사하십시오.
- 모든 공개 구조체의 `Header`와 예약 저장 공간을 초기화하십시오. 구조체를 0으로
  채운 뒤 `StructSize`, `Major`, `Minor`, `Flags`를 설정합니다.
- 핸들과 빌린 뷰는 스코프가 있는 불투명 값으로 취급하십시오. 태스크 스코프
  핸들을 콜백 밖으로 가져가지 말고, 다른 session이나 task에서 사용하지 말며,
  핸들 값을 임의로 만들어내지 마십시오.
- 모든 콜백에서 `NevercStatus`를 반환하십시오. C++ 예외나 호스트 소유 포인터가
  C 경계를 넘게 하지 마십시오.
- `NevercConcurrencyModel`(`SESSION_SERIAL`, `THREAD_SAFE`, `PROCESS_SERIAL`)과
  `NevercReentrancyModel`(`NONE`, `ALLOWED`)은 **가장 좁으면서 정직하게**
  선언하십시오.
- IR, MIR, AST, 그래프, 아티팩트 변경은 반드시 트랜잭션 방식 호스트 API를
  통하십시오. mutation을 시작하고, 변경을 스테이징한 뒤 commit 또는 abort
  합니다. commit은 검증 후 원자적으로 게시하며, 실패한 commit은 이전 상태를
  그대로 남깁니다.
- 가변 상태는 호스트가 제공하는 process/session/task 상태에 두십시오. 전역 가변
  상태는 `utils/plugin-api/check-global-state.py`가 검사합니다.

새 함수는 각각 독립적으로 버전이 매겨진 기능 테이블의 끝에 추가됩니다. 첫 ABI
메이저(`NEVERC_PLUGIN_ABI_MAJOR` = 1) 안에서는 테이블의 안정 접두부가 바뀌지
않습니다.

## 상태와 진단

`NevercStatus`는 `Code`, `Flags`, `Detail` 워드를 담습니다. 주요 코드:

| 코드 | 의미 |
|---|---|
| `NEVERC_STATUS_OK` | 성공. |
| `NEVERC_STATUS_INVALID_ARGUMENT` | 필수 포인터나 값이 없거나 형식이 잘못됨. |
| `NEVERC_STATUS_ABI_MISMATCH` | 협상된 테이블이 너무 작거나 major가 다름. |
| `NEVERC_STATUS_MISSING_INTERFACE` / `CAPABILITY_UNAVAILABLE` | 호스트가 요청한 기능을 제공하지 않음. |
| `NEVERC_STATUS_STALE_HANDLE` / `WRONG_SESSION` / `WRONG_SCOPE` / `WRONG_TYPE` | 핸들이 유효 범위 밖에서 사용됨. |
| `NEVERC_STATUS_POLICY_VIOLATION` | 페이즈 정책이 해당 연산을 허용하지 않음. |
| `NEVERC_STATUS_VERIFICATION_FAILED` | 호스트의 봉인된 검증기가 산출물을 거부함. |
| `NEVERC_STATUS_CANCELLED` / `BUSY` / `RESOURCE_EXHAUSTED` | 협조적 취소 또는 리소스 한계. |

플래그 비트(`RECOVERABLE`, `OUTPUT_ALREADY_COMMITTED`, `OUTPUT_MAY_BE_PARTIAL`,
`OUTPUT_RECOVERY_REQUIRED`, `DURABILITY_UNCONFIRMED`)는 출력에 무슨 일이
일어났는지를 나타내며, 이는 빌드 시스템이 "재시도가 안전한가"를 판단하는 데
필요한 정보입니다.

문제 보고에는 `NevercCoreAPI.EmitDiagnostic`과, 심각도·코드·플러그인 ID·
페이즈 ID·메시지·노트·소스 위치·범위·fix-it을 담은
`NevercDiagnosticDescriptor`를 사용하십시오. 비용이 큰 작업 전에는
`CheckCancelled`를 호출하십시오.

## 예제

전부 빌드하기:

```sh
cmake --build build-neverc --target neverc-pluginsdk-examples
```

각 예제는 두 번 컴파일됩니다 — 설정된 호스트 C 컴파일러로 한 번, 방금 빌드한
NeverC로 한 번 — 그래서 ABI가 양쪽에서 입증됩니다. 모듈은
`build-neverc/neverc/pluginsdk/examples/host/`에 생성됩니다.

| 예제 | CMake 타깃 | 보여주는 내용 |
|---|---|---|
| `DriverTracePlugin.c` | `neverc-plugin-example-driver-trace` | 옵션 등록, 페이즈 관찰, 잡 가로채기 |
| `VirtualHeaderPlugin.c` | `neverc-plugin-example-virtual-header` | 메모리 내 헤더를 제공하는 VFS provider |
| `ASTRewritePlugin.c` | `neverc-plugin-example-ast-rewrite` | 파서 가로채기와 원자적 AST 변경 |
| `ExamplePlugin.c` | `neverc-plugin-example-ir-overview` | 값 커서로 함수 목록을 순회하는 모듈 수준 IR 패스 |
| `FunctionPass.c` | `neverc-plugin-example-function-pass` | 안정적인 IR 함수 패스 |
| `MachinePass.c` | `neverc-plugin-example-machine-pass` | pre-emit 훅의 안정적인 MIR 패스 |
| `MCObserverPlugin.c` | `neverc-plugin-example-mc-observer` | 읽기 전용 MC 방출 이벤트 |
| `ObjectRewritePlugin.c` | `neverc-plugin-example-object-rewrite` | 트랜잭션 방식 ObjectGraph 재작성 |
| `CustomCallConvPlugin.c` | `neverc-plugin-example-custom-callconv` | 데이터 주도 호출 규약 |
| `DynCodeTracePlugin.c` | `neverc-plugin-example-dyncode-trace` | dyncode 파이프라인 관찰 |
| `DynCodeEncoderPlugin.c` | `neverc-plugin-example-dyncode-encoder` | dyncode 문자셋 인코딩 가로채기 |
| `CrtShimPlugin.c` | `neverc-plugin-example-crt-shim` | CRT 의존성이 전혀 없는 플러그인 |
| `BenchPlugin.c` | `neverc-plugin-example-abi-bench` | ABI 호출 처리량 마이크로벤치마크 |

하나 로드해 보기:

```sh
neverc -fplugin=build-neverc/neverc/pluginsdk/examples/host/FunctionPass.so \
  -O2 -c input.c -o input.o
```

## 규범적 출처

| 파일 | 보장하는 내용 |
|---|---|
| `neverc/include/neverc/Plugin/Schema/PhaseSchema.json` | 페이즈 ID, 정책, 안정성, 검증 게이트 |
| `pluginsdk/manifest/plugin.json` | ABI 버전, 인터페이스 ID/버전/안정성, 스키마 다이제스트, 지원 타깃 |
| `pluginsdk/abi/plugin.json` | 호스트 ABI 키별로 측정한 모든 공개 구조체의 크기·정렬·필드 오프셋 |
| `docs/plugin-api/coverage.json` | 각 안정 페이즈를 정상·비정상·교체·옵저버·봉인 게이트 테스트에 매핑 |

따라서 SDK는 호스트에 대해 기계적으로 검증할 수 있고, 플러그인 빌드도 자신이
로드될 ABI 키에 대해 구조체 레이아웃을 단언할 수 있습니다.
