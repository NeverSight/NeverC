**언어**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# NeverC 플러그인 ABI

NeverC 플러그인은 함수를 정확히 하나만 내보내고, 128비트 인터페이스 ID로 버전이
매겨진 기능 테이블을 협상하며, 이름 붙은 컴파일러 페이즈로 이루어진 고정된 그래프
에 자신을 붙이는 공유 모듈입니다. 인터페이스 전체가 순수 C11입니다. 플러그인은
LLVM 헤더를 포함하지 않고, 컴파일러를 링크하지 않으며, C++ 타입을 경계 너머로 전
달하지 않습니다.

```c
NEVERC_EXPORT NevercStatus NEVERC_CALL
neverc_plugin_entry(const NevercBootstrapAPI *Bootstrap,
                    NevercPluginDescriptor *OutPlugin);
```

`PluginCore.h`에 선언된 이 시그니처가 링크 계약의 전부입니다. 그 밖의 모든 것——IR
읽기, 오브젝트 그래프 재작성, 최적화 파이프라인 교체——은 ID로 호스트에 요청하는
테이블을 통해 도달합니다.

## 가이드

| 가이드 | 다루는 범위 |
|---|---|
| [드라이버 API](driver.ko.md) | 명령줄, 툴체인 선택, 액션 그래프, 잡 그래프 |
| [소스와 I/O API](source.ko.md) | VFS 프로바이더, 소스 위치, 버퍼, 출력 싱크, 의존성 |
| [전처리기 API](prep.ko.md) | 토큰, 매크로, pragma, include, 기능 질의, 39가지 이벤트 |
| [AST와 의미 분석 API](ast-sema.ko.md) | 파서 확장, AST 변경, 이름 조회, 타입, 상수 |
| [IR API](ir.ko.md) | LLVM IR 읽기, 트랜잭션 기반 구성, 분석, 패스, 프로바이더 |
| [MIR API](mir.ko.md) | 머신 함수, 레지스터, 스택 프레임, MIR 패스와 분석 |
| [타깃, MC, 어셈블리, 오브젝트](target-mc-object.ko.md) | 타깃 등록, 호출 규약, MC 인코딩, 오브젝트 그래프 |
| [링크와 LTO API](link-lto.ko.md) | 링크 그래프, 심볼 결정, GC/ICF, 링커와 LTO 프로바이더 |
| [DynCode API](dyncode.ko.md) | 평평한 위치 독립 이미지, 임포트 로워링, 문자셋 인코딩 |
| [사용자 정의 호출 규약](custom-callconv/README.ko.md) | 데이터 주도 호출 규약 플러그인 |
| [페이즈 커버리지 근거](coverage.json) | 모든 안정 페이즈에 대한 테스트 매핑 |

## 실행 모델

호스트는 3중으로 중첩된 스코프를 통해 플러그인을 구동합니다. 각 스코프는 플러그인
이 직접 할당하고 소유하는 불투명 상태 포인터를 플러그인에 건네줍니다. 따라서 올바
르게 작성된 플러그인에는 전역 가변 상태가 필요 없습니다.

| 스코프 | 콜백 | 의미 |
|---|---|---|
| Process | `ProcessBegin`, `Register`, `Destroy` | 컴파일러 프로세스 하나. 여기서 인터페이스를 질의하고 기능을 등록합니다. |
| Session | `SessionBegin`, `SessionEnd` | 드라이버 호출 한 번. |
| Task | `TaskBegin`, `TaskEnd` | `NevercTaskKind`로 식별되는 작업 단위 하나. |

```c
typedef struct NevercPluginDescriptor {
  NevercABITableHeader Header;
  NevercStringView PluginID;
  NevercStringView DisplayName;
  NevercSemanticVersion Version;
  NevercConcurrencyModel Concurrency;
  NevercReentrancyModel Reentrancy;
  NevercStructArrayView RequiredInterfaces;   /* NevercInterfaceRequirement[] */
  NevercStructArrayView OptionalInterfaces;   /* NevercInterfaceRequirement[] */
  NevercStructArrayView Dependencies;         /* NevercPluginDependency[]     */
  NevercProcessBeginFn ProcessBegin;
  NevercRegisterPluginFn Register;
  NevercSessionBeginFn SessionBegin;
  NevercSessionEndFn SessionEnd;
  NevercTaskBeginFn TaskBegin;
  NevercTaskEndFn TaskEnd;
  NevercPluginDestroyFn Destroy;
} NevercPluginDescriptor;
```

실질적으로 필수인 것은 `PluginID`와 `Register`뿐이며, 모든 콜백 슬롯은 `NULL`로
남겨 두어도 됩니다. 태스크 종류는 `NEVERC_TASK_INVOCATION`, `TRANSLATION_UNIT`,
`LTO`, `LINK`, `CODEGEN`, `OBJECT`, `DYNCODE`입니다.

호스트는 먼저 `ProcessBegin`을 호출하고, 이어서 `Register`를 정확히 한 번 호출합
니다. 옵션, 옵저버, 인터셉터, 프로바이더를 추가할 수 있는 곳은 등록 시점뿐이며,
그 뒤로 페이즈 그래프는 고정됩니다.

상태는 미리 붙잡아 두는 것이 아니라 콜백 안에서 가져옵니다:

```c
Core->GetSessionState(Core->Context, Frame->Session, PluginID, &SessionState);
Core->GetTaskState(Core->Context, Frame->Task, PluginID, &TaskState);
```

## 페이즈

페이즈란 입력 아티팩트에서 출력 아티팩트로 가는, 이름이 있고 버전이 매겨진 전이입
니다. NeverC는 **130개의 내장 페이즈**를 제공하며, 플러그인이 정의하는 페이즈를
위해 8개의 확장 ID 패밀리를 예약해 두었습니다:

| 도메인 | 페이즈 수 | 도메인 | 페이즈 수 |
|---|--:|---|--:|
| `driver` | 6 | `mir` | 10 |
| `source` | 3 | `codegen` | 4 |
| `prep` | 6 | `mc` | 13 |
| `syntax` | 7 | `assembly` | 4 |
| `sema` | 7 | `object` | 8 |
| `ir` | 8 | `link` | 20 |
| | | `dyncode` | 34 |

이 130개는 ABI 메이저 1에서 모두 안정성 등급 `stable`입니다. 각 페이즈는 정책을
공표하며, 플러그인은 그 정책이 허용하는 방식으로만 자신을 붙일 수 있습니다:

| 정책 플래그 | 페이즈 수 | 플러그인이 할 수 있는 일 |
|---|--:|---|
| `NEVERC_PHASE_OBSERVABLE` | 130 | 읽기 전용 통지를 받는 옵저버를 등록한다. |
| `NEVERC_PHASE_INTERCEPTABLE` | 105 | 페이즈를 감싸고 체인의 나머지를 호출할지 결정한다. |
| `NEVERC_PHASE_REPLACEABLE` | 86 | 출력 자체를 공급하는 프로바이더를 등록한다. |
| `NEVERC_PHASE_SKIPPABLE_WITH_PROOF` | 13 | 증명 핸들을 제공하면서 전이를 건너뛴다. |
| `NEVERC_PHASE_SEALED_HOST_GATE` | 14 | 아무것도 할 수 없다. 검증기와 커밋은 호스트 소유다. |

봉인된 게이트 14개는 `ir.final_verify`, `mir.final_verify`,
`codegen.product_verify`, `assembly.final_verify`, `assembly.commit`,
`object.final_verify`, `object.commit`, `link.image_verify`,
`link.side_outputs_verify`, `link.commit`, `dyncode.ir.final_verify`,
`dyncode.mir.final_verify`, `dyncode.verify`, `dyncode.commit`입니다. 관찰은 할
수 있지만 가로채기, 교체, 건너뛰기는 결코 할 수 없습니다.

옵저버는 페이즈가 선언한 시점에 전달됩니다: `NEVERC_OBSERVER_BEFORE`,
`NEVERC_OBSERVER_AFTER`, `NEVERC_OBSERVER_AFTER_COMMIT`. 인터셉터는
`NevercPhaseContinuation`을 받으며, 콜백 스레드에서 `InvokeNext`를 **많아야 한
번** 호출한 뒤 `NevercPhaseResult.Action`에 `NEVERC_PHASE_CONTINUE`,
`NEVERC_PHASE_REPLACE`, `NEVERC_PHASE_SKIP` 중 하나를 보고해야 합니다.

모든 페이즈 콜백은 동일한 프레임을 받습니다:

```c
typedef struct NevercPhaseFrame {
  NevercABITableHeader Header;
  NevercSessionHandle Session;
  NevercTaskHandle Task;
  NevercInterfaceID Phase;
  NevercPhaseRoute Route;        /* triple, CPU, features, object format */
  NevercArtifactHandle Input;
  NevercArtifactHandle CurrentOutput;
  NevercHandle Cancellation;
} NevercPhaseFrame;
```

`Schema/PhaseSchema.json`이 페이즈 ID, 정책, 안정성 등급, 검증기 게이트의 규범적
출처입니다. 생성되는 `Schema/PluginPhaseSchema.inc`는 그것들을 각각 컴파일 타임
상수로 노출합니다——페이즈 `neverc.ir.pass.pipeline_start`의 경우:

```c
NEVERC_PHASE_IR_PASS_PIPELINE_START_NAME       /* "neverc.ir.pass.pipeline_start" */
NEVERC_PHASE_IR_PASS_PIPELINE_START_HIGH       /* UINT64_C(0x4e43504849520001)     */
NEVERC_PHASE_IR_PASS_PIPELINE_START_LOW        /* UINT64_C(0x0000000000000004)     */
NEVERC_PHASE_IR_PASS_PIPELINE_START_POLICY     /* OBSERVABLE | INTERCEPTABLE       */
NEVERC_PHASE_IR_PASS_PIPELINE_START_STABILITY
NEVERC_PHASE_IR_PASS_PIPELINE_START_INPUT_HIGH /* and _INPUT_LOW, _OUTPUT_*        */
```

`NEVERC_BUILTIN_PHASE_COUNT`와 도메인별
`NEVERC_BUILTIN_<DOMAIN>_PHASE_COUNT` 상수를 쓰면, 플러그인이 빌드 시점에 전제한
그래프를 단언할 수 있습니다.

## 완전한 최소 플러그인

아래는 `pluginsdk/templates/minimal/Plugin.c` 원문 그대로입니다. 로드되고, ABI를
협상하고, 아무것도 등록하지 않고, 깔끔하게 언로드됩니다——이 디렉터리를 복사해서
여기서부터 키워 나가세요.

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
  /* Register options, observers, interceptors, or providers here. */
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

`OutPlugin`은 호출자가 소유하는 버퍼입니다. 진입 시 그 `Header.StructSize`는 쓰기
가능한 용량을 뜻합니다. 플러그인은 그만큼까지만 쓰고, 실제로 생성한 크기를 보고합
니다. 디스크립터 자신의 `Header`를 먼저 쓴 다음 복사를 잘라 내면, 이 규칙의 양쪽
을 동시에 만족시킬 수 있습니다.

## 인터페이스 협상

기능 테이블은 심볼이 아니라 128비트 인터페이스 ID로 가져옵니다. 빌드할 때 전제한
메이저 버전과, 동작 가능한 최소 마이너 버전을 요청하세요:

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

호출할 마지막 함수의 오프셋과 `TableSize`를 비교하는 것——이것이 이 ABI를 확장 가
능하게 만드는 규칙입니다. 더 새로운 호스트는 필드를 뒤에 덧붙이고, 더 오래된 플러
그인은 자신이 검증한 앞부분 너머를 결코 읽지 않으므로 계속 동작합니다.
`NEVERC_ABI_FIELD_AVAILABLE(header, type, field)` 매크로는 여러분이 받은 구조체에
같은 검사를 적용합니다. 동일한 시그니처의 `QueryInterface`가 `NevercCoreAPI`에도
있으므로, 진입 시점이 아니라 나중에 협상할 수도 있습니다.

공개 인터페이스와 그 테이블, ID 매크로:

| 인터페이스 매크로 쌍 | 테이블 | 헤더 |
|---|---|---|
| `NEVERC_INTERFACE_CORE_{HIGH,LOW}` | `NevercCoreAPI` | `PluginCore.h` |
| `NEVERC_INTERFACE_DRIVER_*` | `NevercDriverAPI` | `PluginDriver.h` |
| `NEVERC_INTERFACE_IO_*`, `..._SOURCE_LOCATION_*` | `NevercIOAPI`, `NevercSourceLocationAPI` | `PluginSource.h` |
| `NEVERC_INTERFACE_PREP_*` | `NevercPrepAPI` | `PluginPrep.h` |
| `NEVERC_INTERFACE_AST_*`, `..._PARSER_*` | `NevercASTAPI`, `NevercParserAPI` | `PluginAST.h` |
| `NEVERC_INTERFACE_SEMA_*` | `NevercSemaAPI` | `PluginSema.h` |
| `NEVERC_INTERFACE_IR_CORE_*`, `..._IR_BUILDER_*`, `..._IR_ANALYSIS_*`, `..._IR_PASS_*`, `..._IR_GEN_*`, `..._IR_OPTIMIZATION_*` | IR 테이블 6개 | `PluginIR.h` |
| `NEVERC_INTERFACE_TARGET_*`, `..._TARGET_ABI_*`, `..._CALLING_CONVENTION_*` | `NevercTargetAPI`, `NevercTargetABIAPI`, `NevercCallingConventionAPI` | `PluginTarget.h` |
| `NEVERC_INTERFACE_MIR_*`, `..._MIR_ANALYSIS_*`, `..._MIR_PASS_*`, `..._MIR_PROVIDER_*` | MIR 테이블 4개 | `PluginMIR.h` |
| `NEVERC_INTERFACE_MC_*`, `..._MC_EMISSION_*`, `..._MC_PROVIDER_*`, `..._ASSEMBLY_PROVIDER_*` | MC 테이블 4개 | `PluginMC.h` |
| `NEVERC_INTERFACE_OBJECT_*`, `..._OBJECT_FORMAT_*`, `..._OBJECT_PHASE_*` | 오브젝트 테이블 3개 | `PluginObject.h` |
| `NEVERC_INTERFACE_LINK_*`, `..._LINK_REGISTRAR_*`, `..._LINK_PHASE_*` | 링크 테이블 3개 | `PluginLink.h` |
| `NEVERC_INTERFACE_LTO_*`, `..._LTO_REGISTRAR_*` | `NevercLTOAPI`, `NevercLTORegistrarAPI` | `PluginLTO.h` |
| `NEVERC_INTERFACE_DYNCODE_*`, `..._DYNCODE_REGISTRAR_*`, `..._DYNCODE_PHASE_*` | dyncode 테이블 3개 | `PluginDynCode.h` |

각 헤더는 `QueryInterface`에 넘겨야 할 대응 `NEVERC_<DOMAIN>_API_MAJOR`와
`_MINOR`도 정의합니다.

인터페이스는 `NEVERC_INTERFACE_STABLE`(더 새로운 호스트는 추가만 가능)이거나
`NEVERC_INTERFACE_LOCKSTEP`(정확히 일치해야 하는 타깃별 스키마)입니다. LOCKSTEP
값을 사용하기 전에 스키마 다이제스트를 비교하세요.

## 등록

`Register`는 `NevercRegistrarAPI`와 불투명한 `RegistrarContext`를 받습니다:

```c
typedef struct NevercRegistrarAPI {
  NevercABITableHeader Header;
  NevercRegisterInterfaceFn RegisterInterface;
  NevercRegisterPhaseFn RegisterPhase;
  NevercRegisterObserverFn RegisterObserver;
  NevercRegisterInterceptorFn RegisterInterceptor;
  NevercRegisterProviderFn RegisterProvider;
  NevercRegisterOptionFn RegisterOption;
} NevercRegistrarAPI;
```

도메인별 등록 함수——`NevercIRPassAPI.RegisterPass`,
`NevercTargetAPI.RegisterTarget`, `NevercObjectFormatAPI.RegisterFormat` 등——는
모두 같은 `RegistrarContext`를 두 번째 인자로 받습니다. 호스트는 이를 통해 등록을
여러분의 플러그인에 귀속시킵니다.

프로바이더는 빌드 캐시가 의존하는 결정성 계약을 추가로 선언합니다:

```c
Provider.ProviderID    = SV("com.example.my-lowering");
Provider.Route         = /* triple / CPU / features / object format */;
Provider.Deterministic = NEVERC_TRUE;
Provider.Cacheable     = NEVERC_TRUE;
Provider.FallbackSafe  = NEVERC_FALSE;  /* built-in cannot silently take over */
```

## 빌드

통합 헤더를 포함하거나, 사용하는 도메인만 포함하세요:

```c
#include "neverc/Plugin/NevercPluginAPI.h"   /* everything */
#include "neverc/Plugin/PluginIR.h"          /* or one domain */
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

호스트에 맞게 `.so`, `.dylib`, `.dll`을 사용하세요. 이 SDK는 LLVM도 NeverC 런타임
도 링크하지 않습니다——`NevercPluginSDK::headers`는 헤더 전용입니다.

## 로딩과 설정

```sh
neverc -fplugin=./MyPlugin.dylib -c input.c -o input.o
```

| 옵션 | 형태 | 용도 |
|---|---|---|
| `-fplugin=<path>` | 반복 가능 | 전체 툴체인 플러그인 공유 모듈을 로드한다. |
| `-fplugin-arg=<plugin-id>:<key>=<value>` | 반복 가능 | 등록된 플러그인 옵션에 네임스페이스가 붙은 값을 전달한다. |
| `-fplugin-provider=<phase>:<plugin-id>` | 반복 가능 | 교체 가능한 페이즈를 어느 플러그인이 제공할지 고른다. |
| `-fplugin-pass=<dsopath>` | 반복 가능 | C-ABI 아웃오브트리 패스 플러그인을 로드한다. |
| `-fplugin-pass-arg=<key>=<value>` | 반복 가능 | C-ABI 패스 플러그인에 인자를 전달한다. |

`<plugin-id>:` 한정자를 생략할 수 있는 것은 활성 플러그인이 정확히 하나일 때뿐입
니다. 플러그인이 `RegisterOption`으로 등록한 옵션은 선언한 철자 그대로도 받아들여
지며, flag·joined·separate·다중 인자 형태를 지원합니다. 대응하는 `-fplugin=` 없이
주어진 플러그인 인자나 프로바이더 선택은 조용한 무시가 아니라 하드 에러입니다.

등록된 옵션은 언제든 core 테이블을 통해 다시 읽을 수 있습니다:

```c
uint64_t Count = 0;
Core->GetPluginOptionValueCount(Core->Context, Session, PluginID,
                                SV("--driver-trace"), &Count);
NevercStringView Value;
Core->GetPluginOptionValue(Core->Context, Session, PluginID,
                           SV("--driver-trace"), 0, &Value);
```

## ABI 규칙

- 기능 테이블은 `QueryInterface`로 질의하고, 메이저 일치를 요구하며, 필드를 건드
  리기 전에 `StructSize`를 확인한다.
- 모든 공개 구조체의 `Header`와 예약 영역을 초기화한다. 구조체를 0으로 채운 뒤
  `StructSize`, `Major`, `Minor`, `Flags`를 설정한다.
- 핸들과 빌린 뷰는 스코프가 있는 불투명 값으로 다룬다. 태스크 스코프 핸들을 콜백
  이후까지 보관하지 말고, 다른 세션이나 태스크에서 쓰지 말며, 핸들 값을 지어내지
  않는다.
- 모든 콜백에서 `NevercStatus`를 반환한다. C++ 예외나 호스트 소유 포인터가 C 경계
  를 넘지 않게 한다.
- 사실에 부합하는 가장 좁은 `NevercConcurrencyModel`(`SESSION_SERIAL`,
  `THREAD_SAFE`, `PROCESS_SERIAL`)과 `NevercReentrancyModel`(`NONE`, `ALLOWED`)
  을 선언한다.
- IR, MIR, AST, 그래프, 아티팩트 변경은 트랜잭션 기반 호스트 API로 수행한다. 변경
  을 시작하고, 변경을 스테이징한 뒤, 커밋하거나 중단한다. 커밋은 원자적으로 검증
  하고 게시하며, 실패한 커밋은 이전 상태를 그대로 남긴다.
- 호스트가 메모리를 계상하도록 하려면 `NevercCoreAPI.Allocate` / `Reallocate` /
  `Deallocate`로 할당한다.
- 가변 상태는 호스트가 제공하는 process/session/task 상태에 둔다. 전역 가변 상태
  는 `utils/plugin-api/check-global-state.py`가 검사한다.

모든 공개 구조체는 `NEVERC_ABI_PACK_BEGIN`(8바이트 패킹) 아래에 배치되며 고정 폭
타입만 사용합니다. 새 함수는 독립적으로 버전이 매겨진 기능 테이블 끝에 덧붙습니
다. 첫 ABI 메이저(`NEVERC_PLUGIN_ABI_MAJOR` = 1) 안에서는 테이블의 안정된 앞부분
이 바뀌지 않습니다.

## 상태와 진단

`NevercStatus`는 `Code`, `Flags`, `Detail` 워드를 담습니다. 코드 전체 집합:

| 코드 | 의미 |
|---|---|
| `NEVERC_STATUS_OK` | 성공. |
| `NEVERC_STATUS_INVALID_ARGUMENT` | 필수 포인터나 값이 없거나 잘못됨. |
| `NEVERC_STATUS_ABI_MISMATCH` | 협상된 테이블이 너무 작거나 메이저가 다름. |
| `NEVERC_STATUS_MISSING_INTERFACE` | 호스트가 요청된 인터페이스를 게시하지 않음. |
| `NEVERC_STATUS_VERSION_MISMATCH` | 요청된 메이저/마이너를 만족할 수 없음. |
| `NEVERC_STATUS_INVALID_DESCRIPTOR` | 디스크립터가 구조 검증에 실패함. |
| `NEVERC_STATUS_DUPLICATE_ID` | 해당 ID가 이미 등록됨. |
| `NEVERC_STATUS_DEPENDENCY_MISSING` | 선언된 의존성이 없음. |
| `NEVERC_STATUS_DEPENDENCY_CYCLE` | 등록 순서를 만족할 수 없음. |
| `NEVERC_STATUS_BUSY` | 자원이 다른 곳에서 점유 중. |
| `NEVERC_STATUS_CANCELLED` | 협조적 취소가 요청됨. |
| `NEVERC_STATUS_RESOURCE_EXHAUSTED` | 예산이나 한도에 도달함. |
| `NEVERC_STATUS_STALE_HANDLE` | 핸들이 가리키던 객체보다 오래 살아남음. |
| `NEVERC_STATUS_WRONG_SESSION` | 핸들이 다른 세션에서 사용됨. |
| `NEVERC_STATUS_WRONG_SCOPE` | 핸들이 스코프 밖에서 사용됨. |
| `NEVERC_STATUS_WRONG_TYPE` | 핸들이 다른 종류의 엔티티를 가리킴. |
| `NEVERC_STATUS_INVALID_STATE` | 현재 상태에서 허용되지 않는 연산. |
| `NEVERC_STATUS_POLICY_VIOLATION` | 페이즈 정책이 그 연산을 금지함. |
| `NEVERC_STATUS_VERIFICATION_FAILED` | 봉인된 호스트 검증기가 산출물을 거부함. |
| `NEVERC_STATUS_CAPABILITY_UNAVAILABLE` | 호스트가 여기서 그 기능을 제공할 수 없음. |
| `NEVERC_STATUS_PLUGIN_FAILURE` | 플러그인이 일반적 실패를 보고함. |
| `NEVERC_STATUS_PLUGIN_EXCEPTION` | 플러그인 콜백에서 예외가 빠져나옴. |
| `NEVERC_STATUS_OUTPUT_PARTIAL` | 출력이 일부만 기록됨. |
| `NEVERC_STATUS_REENTRANCY_DENIED` | 재진입 호출이 거부됨. |
| `NEVERC_STATUS_NOT_FOUND` | 지정한 엔티티가 존재하지 않음. |

플래그 비트는 출력에 무슨 일이 있었는지를 알려 주며, 이는 빌드 시스템이 재시도가
안전한지 판단하는 데 필요한 정보입니다: `NEVERC_STATUS_FLAG_RECOVERABLE`,
`_OUTPUT_ALREADY_COMMITTED`, `_OUTPUT_MAY_BE_PARTIAL`,
`_OUTPUT_RECOVERY_REQUIRED`, `_DURABILITY_UNCONFIRMED`.

문제 보고에는 `NevercCoreAPI.EmitDiagnostic`과, 심각도(`NOTE`, `REMARK`,
`WARNING`, `ERROR`, `FATAL`)·코드·플러그인 ID·페이즈 ID·메시지·노트·소스 위치·범
위·fix-it을 담은 `NevercDiagnosticDescriptor`를 사용하세요. 비용이 큰 작업 전에는
`CheckCancelled`를 호출하세요.

## 예제

전부 빌드하기:

```sh
cmake --build build-neverc --target neverc-pluginsdk-examples
```

모든 예제는 두 번 컴파일됩니다——한 번은 설정된 호스트 C 컴파일러로, 또 한 번은 방
금 빌드한 NeverC로——그래서 ABI가 양쪽 모두에서 증명됩니다. 모듈은
`build-neverc/neverc/pluginsdk/examples/host/`에 생성됩니다.

| 예제 | CMake 타깃 | 보여 주는 것 |
|---|---|---|
| `DriverTracePlugin.c` | `neverc-plugin-example-driver-trace` | 옵션 등록, 페이즈 관찰, 잡 가로채기 |
| `VirtualHeaderPlugin.c` | `neverc-plugin-example-virtual-header` | 메모리 내 헤더를 제공하는 VFS 프로바이더 |
| `ASTRewritePlugin.c` | `neverc-plugin-example-ast-rewrite` | 파서 가로채기와 원자적 AST 변경 |
| `ExamplePlugin.c` | `neverc-plugin-example-ir-overview` | 값 커서로 함수 목록을 순회하는 모듈 수준 IR 패스 |
| `FunctionPass.c` | `neverc-plugin-example-function-pass` | 안정적인 IR 함수 패스 |
| `MachinePass.c` | `neverc-plugin-example-machine-pass` | pre-emit 훅의 안정적인 MIR 패스 |
| `MCObserverPlugin.c` | `neverc-plugin-example-mc-observer` | 읽기 전용 MC 방출 이벤트 |
| `ObjectRewritePlugin.c` | `neverc-plugin-example-object-rewrite` | 트랜잭션 기반 ObjectGraph 재작성 |
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

| 파일 | 보장하는 것 |
|---|---|
| `neverc/include/neverc/Plugin/Schema/PhaseSchema.json` | 페이즈 ID, 정책, 안정성, 검증기 게이트 |
| `pluginsdk/manifest/plugin.json` | ABI 버전, 인터페이스 ID/버전/안정성, 스키마 다이제스트, 지원 타깃 |
| `pluginsdk/abi/plugin.json` | 호스트 ABI 키별로 측정한 모든 공개 구조체의 크기, 정렬, 필드 오프셋 |
| `docs/plugin-api/coverage.json` | 각 안정 페이즈를 긍정·부정·교체·옵저버·봉인 게이트 테스트에 매핑 |

따라서 SDK는 호스트에 대해 기계적으로 검증할 수 있고, 플러그인 빌드는 자신이 로드
될 ABI 키에 대해 구조체 레이아웃을 단언할 수 있습니다.
