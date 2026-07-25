**언어**: [English](driver.md) | [简体中文](driver.zh-CN.md) | [繁體中文](driver.zh-TW.md) | [日本語](driver.ja.md) | [한국어](driver.ko.md) | [Français](driver.fr.md) | [Deutsch](driver.de.md) | [Español](driver.es.md) | [Italiano](driver.it.md) | [Русский](driver.ru.md) | [العربية](driver.ar.md)

[← NeverC 플러그인 ABI](README.ko.md)

# NeverC 플러그인 Driver API

드라이버는 명령줄을 실행 가능한 작업(job) 집합으로 바꿉니다. [`PluginDriver.h`]
는 그 파이프라인을 여섯 개의 단계와 하나의 기능 테이블 `NevercDriverAPI` 로
공개하므로, 플러그인은 인자를 다시 쓰고, 툴체인을 고르고, 액션 그래프를 재구성
하고, 작업을 추가하거나 교체하고, 심지어 프로세스를 새로 띄우는 대신 작업을
인프로세스로 실행할 수도 있습니다.

## 인터페이스

```c
#include "neverc/Plugin/PluginDriver.h"

Bootstrap->QueryInterface(
    Bootstrap->Context,
    (NevercInterfaceID){NEVERC_INTERFACE_DRIVER_HIGH,
                        NEVERC_INTERFACE_DRIVER_LOW},
    NEVERC_DRIVER_API_MAJOR, NEVERC_DRIVER_API_MINOR,
    &Table, &Minor, &TableSize);
```

`NevercDriverAPI` 는 67 개의 함수 슬롯으로 이루어진 하나의 평평한 테이블이며,
원시 인자, 파싱된 옵션, 툴체인 선택, 액션 그래프, 작업 그래프의 다섯 영역으로
묶여 있습니다. `TableSize` 는 사용하려는 마지막 슬롯의 오프셋과 비교해 검증
하세요. 현재 꼬리는 `GetJobResult` 입니다.

## 여섯 개의 드라이버 단계

| 단계 | 정책 | 입력 → 출력 |
|---|---|---|
| `neverc.driver.raw_arguments` | OBSERVABLE, INTERCEPTABLE | argv → argv |
| `neverc.driver.parsed_arguments` | OBSERVABLE, INTERCEPTABLE | 파싱된 옵션 목록 → 파싱된 옵션 목록 |
| `neverc.driver.select_toolchain` | 추가로 REPLACEABLE | 툴체인 요청 → 툴체인 선택 |
| `neverc.driver.build_actions` | 추가로 REPLACEABLE | 요청 → 액션 그래프 |
| `neverc.driver.build_jobs` | 추가로 REPLACEABLE | 액션 그래프 → 작업 그래프 |
| `neverc.driver.execute_job` | 추가로 REPLACEABLE | 작업 실행 요청 → 작업 결과 |

매크로는 언제나 같은 규칙을 따릅니다:
`NEVERC_PHASE_DRIVER_RAW_ARGUMENTS_{NAME,HIGH,LOW,POLICY,…}`.

## 옵션 등록

옵션은 `Register` 중에 단 한 번 선언하며, 그 뒤로 드라이버는 마치 내장 옵션인
것처럼 명령줄에서 그것을 받아들입니다.

```c
typedef struct NevercOptionDescriptor {
  NevercABITableHeader Header;
  NevercStringView Spelling;
  NevercStringList Aliases;
  NevercOptionForm Form;                  /* FLAG, JOINED, SEPARATE, MULTI_ARG */
  NevercOptionValueType ValueType;        /* BOOL, INT, UINT, STRING, ENUM, PATH */
  NevercOptionMultiplicity Multiplicity;  /* SINGLE, LAST_WINS, APPEND */
  uint32_t ArgumentCount;
  NevercBool Required;
  NevercBool Hidden;
  NevercStringView Help;
  NevercStringView Metavar;
  NevercStructArrayView EnumValues;       /* NevercOptionEnumValue[] */
  NevercStringList Conflicts;
  NevercStringList Requires;
  NevercStringView TargetPredicate;
  NevercOptionValidatorFn Validator;
  void *UserData;
  NevercDestroyUserDataFn DestroyUserData;
} NevercOptionDescriptor;
```

[`pluginsdk/examples/DriverTracePlugin.c`] 에서 발췌:

```c
NevercOptionDescriptor Option = {0};
Option.Header = (NevercABITableHeader){sizeof(Option), NEVERC_DRIVER_API_MAJOR,
                                       NEVERC_DRIVER_API_MINOR, 0};
Option.Spelling     = SV("--driver-trace");
Option.Form         = NEVERC_OPTION_FLAG;
Option.ValueType    = NEVERC_OPTION_BOOL;
Option.Multiplicity = NEVERC_OPTION_SINGLE;
Option.Help         = SV("enable the driver trace example plugin");
Status = Registrar->RegisterOption(RegistrarContext, &Option);
```

`Validator` 는 출현할 때마다 호출되며, 플러그인 ID·표기·타깃 트리플·출현
인덱스를 담은 `NevercOptionValidationContext` 를 받습니다. 덕분에 나중에 가서
실패하는 대신 제대로 된 진단으로 값을 거부할 수 있습니다. `TargetPredicate` 는
옵션을 일치하는 트리플로 제한합니다. 값을 다시 읽을 때는
`NevercCoreAPI.GetPluginOptionValueCount` 와 `GetPluginOptionValue` 를 씁니다.

## 원시 인자

`neverc.driver.raw_arguments` 에서 아티팩트는 argv 벡터입니다. 읽기는 인덱스
기반이며, 각 항목은 자신이 어디서 왔는지 보고합니다:

```c
Driver->GetArgumentCount(Driver->Context, Frame, Frame->Input, &Count);

NevercStringView Value, Source;
NevercArgumentOrigin Origin;   /* COMMAND_LINE, CONFIGURATION, PLUGIN */
uint64_t Position;
Driver->GetArgument(Driver->Context, Frame, Frame->Input, Index,
                    &Value, &Origin, &Source, &Position);
```

편집은 트랜잭션 방식이며 인터셉터 안에서만 합법입니다. 변경이 continuation 에
묶이기 때문입니다:

```c
NevercArgumentMutationHandle Mutation;
Driver->BeginArgumentMutation(Driver->Context, Frame, Continuation,
                              Frame->Input, &Mutation);
Driver->InsertArgument(Driver->Context, Mutation, Index, SV("-O2"));
Driver->ReplaceArgument(Driver->Context, Mutation, Index, SV("-O3"));
Driver->EraseArgument(Driver->Context, Mutation, Index);
Driver->CommitArgumentMutation(Driver->Context, Mutation);  /* 또는 Abort */
```

## 파싱된 인자

`neverc.driver.parsed_arguments` 는 문자열이 아니라 옵션의 출현 단위로 동작
합니다. 다시 렉싱되면 안 되는 플래그를 추가할 때 필요한 계층이 바로 이것입니다:

```c
typedef struct NevercOptionOccurrence {
  NevercABITableHeader Header;
  uint64_t Occurrence;
  NevercStringView Spelling;
  NevercStringList Values;
  NevercArgumentOrigin Origin;
  uint32_t Reserved;
} NevercOptionOccurrence;
```

읽기는 `GetOptionOccurrenceCount` 와 `GetOptionOccurrence`, 편집은
`BeginParsedArgumentMutation`, `AddOptionOccurrence`,
`RemoveOptionOccurrence`, `ReplaceOptionOccurrence` 에 이어
`CommitParsedArgumentMutation` / `AbortParsedArgumentMutation` 입니다.

## 툴체인 선택

요청은 무엇이 요구되었고 드라이버가 무엇을 계산했는지를 함께 기술합니다:

```c
typedef struct NevercToolChainRequest {
  NevercABITableHeader Header;
  NevercStringView RequestedTriple;
  NevercStringView ComputedTriple;
  NevercStringView SysRoot;
  NevercStringView ResourceDir;
  NevercStringView CPU;
  NevercStringList Features;
  NevercExecutionLevel ExecutionLevel;  /* UNSPECIFIED, USER, KERNEL */
  NevercBool DynamicCodeProfile;
  uint32_t Reserved;
} NevercToolChainRequest;
```

인터셉터는 `BeginToolChainMutation`, `SetToolChainTriple`, `SetToolChainCPU`,
`SetToolChainFeatures`, `CommitToolChainMutation` 으로 요청을 조정할 수 있습니
다. 반면 프로바이더는 `CreateToolChainSelection` 으로 단계 자체에 답하며, 내장
툴체인 ID 중 하나 또는 자체 ID 를 지목합니다:

```c
NEVERC_TOOLCHAIN_ID_DARWIN        /* "neverc.builtin.darwin"      */
NEVERC_TOOLCHAIN_ID_LINUX         /* "neverc.builtin.linux"       */
NEVERC_TOOLCHAIN_ID_MSVC          /* "neverc.builtin.msvc"        */
NEVERC_TOOLCHAIN_ID_GENERIC_ELF   /* "neverc.builtin.generic-elf" */
NEVERC_TOOLCHAIN_ID_MACHO         /* "neverc.builtin.macho"       */
NEVERC_TOOLCHAIN_ID_GENERIC_GCC   /* "neverc.builtin.generic-gcc" */
```

`GetToolChainSelection` 은 결과를 되읽으면서 `BuiltinProviderUsed` 를 보고하므
로, 옵서버는 해당 단계를 플러그인이 가져갔는지 알 수 있습니다.

## 액션 그래프

액션 노드는 타입이 붙은 컴파일 단계입니다. 노드는 드라이버 입력과 다른 노드를
참조합니다:

```c
typedef struct NevercActionNode {
  NevercABITableHeader Header;
  NevercActionNodeID Node;
  NevercActionKind Kind;
  NevercDriverType OutputType;
  uint64_t InputCount;
  NevercDriverInputID DriverInput;
  NevercStringView BindArch;
  uint64_t Reserved;
} NevercActionNode;
```

| `NevercActionKind` | | `NevercDriverType` | |
|---|---|---|---|
| `INPUT` | `BIND_ARCH` | `PP_C`, `C`, `C_HEADER` | `PP_ASM`, `ASM` |
| `PREPROCESS` | `COMPILE` | `LLVM_IR`, `LLVM_BC` | `LTO_IR`, `LTO_BC` |
| `BACKEND` | `ASSEMBLE` | `OBJECT`, `IMAGE` | `DSYM` |
| `LINK`, `LIPO` | `DSYMUTIL` | `DEPENDENCIES` | `NOTHING` |
| `STATIC_LIB` | `DYNCODE` | | |

읽기는 `GetDriverInputCount` / `GetDriverInput`, `GetActionNodeCount` /
`GetActionNode` / `GetActionNodeInput`, `GetActionRootCount` /
`GetActionRoot` 를 사용합니다.

대체 그래프를 만들 때는 빌더를 거친 뒤 한 번에 발행합니다:

```c
NevercActionGraphBuilderHandle Builder;
Driver->CreateActionGraphBuilder(Driver->Context, Frame, Request, &Builder);

NevercActionNodeDescriptor Node = {0};
Node.Header     = (NevercABITableHeader){sizeof(Node), NEVERC_DRIVER_API_MAJOR,
                                         NEVERC_DRIVER_API_MINOR, 0};
Node.Kind       = NEVERC_ACTION_COMPILE;
Node.OutputType = NEVERC_DRIVER_TYPE_OBJECT;
Node.Inputs     = /* NevercActionNodeIDList */;
NevercActionNodeID Created;
Driver->AddActionNode(Driver->Context, Builder, &Node, &Created);

Driver->SetActionRoots(Driver->Context, Builder, Roots);
Driver->PublishActionGraph(Driver->Context, Frame, Builder, &OutGraph);
```

`RemoveActionNode`, `ReplaceActionNodeInputs`, `SetActionNodeOutputType`,
`SetActionNodeBindArch` 는 진행 중인 빌더를 편집합니다. 새로 만드는 대신 호스트
의 기존 그래프를 조정하려면 `BeginActionGraphMutation` 과
`CommitActionGraphMutation` 을 쓰세요. 두 형태 모두 `AbortActionGraphEdit` 로
버릴 수 있습니다.

## 작업 그래프

작업은 실행할 명령입니다. `NevercJobDescriptor` 가 그 하나를 기술합니다:

```c
typedef struct NevercJobDescriptor {
  NevercABITableHeader Header;
  NevercJobKind Kind;                             /* COMMAND, FRONTEND, LINKER,
                                                     ARCHIVE, PLUGIN, DYNCODE  */
  NevercResponseFileKind ResponseFileKind;        /* NONE, FULL, LIST          */
  NevercResponseFileEncoding ResponseFileEncoding;/* UTF8, CURRENT_CODE_PAGE,
                                                     UTF16                     */
  NevercBool InProcess;
  NevercActionNodeID SourceAction;
  NevercLinkerFlavor LinkerFlavor;                /* NONE, GNU, WIN_LINK, DARWIN */
  uint32_t Reserved;
  NevercStringView Executable;
  NevercStringList Arguments;
  NevercStringList Environment;
  NevercJobFileList Inputs;
  NevercJobFileList Outputs;
  NevercJobIDList Dependencies;
  NevercStringView CallbackID;
  NevercPluginJobCallbackFn Callback;
  void *UserData;
} NevercJobDescriptor;
```

`Kind` 를 `NEVERC_JOB_PLUGIN` 으로 두고 `Callback` 을 주면, 드라이버는 원래
프로세스를 띄웠을 자리에서 여러분의 함수를 실행합니다:

```c
static NevercStatus NEVERC_CALL run_job(const NevercPluginJobContext *Context,
                                        int32_t *OutExitCode, void *UserData) {
  /* Context->Arguments, ->Environment, ->Inputs, ->Outputs 는 빌린 값입니다. */
  *OutExitCode = 0;
  return neverc_status_ok();
}
```

그래프 읽기는 액션 그래프와 같은 모양입니다: `GetJobCount` / `GetJob`,
`GetJobDependency`, `GetJobArgument` / `GetJobEnvironment`, `GetJobInput` /
`GetJobOutput`. `NevercJob` 은 개수만 보고한다는 점에 유의하세요. 인라인 배열을
기대하지 말고 문자열이나 파일은 인덱스로 가져와야 합니다.

편집은 `CreateJobGraphBuilder` 나 `BeginJobGraphMutation` 으로 시작해 `AddJob`,
`RemoveJob`, `MoveJobBefore`, `ReplaceJob`, `SetJobArgument`,
`SetJobEnvironment`, `SetJobInput`, `SetJobOutput`,
`ReplaceJobDependencies` 를 씁니다. 발행은 `PublishJobGraph` 또는
`CommitJobGraphMutation`, 폐기는 `AbortJobGraphEdit` 입니다.

## 작업 실행

`neverc.driver.execute_job` 에서 입력 아티팩트는 `NevercJobExecutionRequest`
입니다. 즉 작업 자체에 더해 완전히 구체화된 인자·환경·입력·출력·의존성 목록이
함께 옵니다. 프로바이더는 작업을 실행하고 결과를 보고합니다:

```c
typedef struct NevercJobResultDescriptor {
  NevercABITableHeader Header;
  int32_t ExitCode;
  NevercBool ExecutionFailed;
  NevercBool HasProcessStatistics;
  uint32_t Reserved;
  NevercStringView ErrorMessage;
  NevercOutputSealList OutputSeals;
  uint64_t TotalTimeMicroseconds;
  uint64_t UserTimeMicroseconds;
  uint64_t PeakMemoryKiB;
} NevercJobResultDescriptor;
```

`OutputSeals` 는 I/O API 를 통해 만들어진 `NevercOutputSealHandle` 들을 담습니다
([Source 와 I/O](source.ko.md) 참고). 호스트는 이를 통해 작업이 썼다고 주장한
파일이 보고된 다이제스트 그대로 실제로 존재하는지 확인합니다. `GetJobResult` 는
커밋된 결과를 읽으며, 툴체인 선택과 마찬가지로 `BuiltinProviderUsed` 를 보고
합니다.

## 실전 예제: 인자 관찰과 작업 실행 가로채기

[`pluginsdk/examples/DriverTracePlugin.c`] 를 압축한 것입니다. 이 플러그인은
전역 변수를 전혀 두지 않습니다. 프로세스 상태가 협상된 테이블을 보관하고,
세션·태스크 단위 카운터는 각 콜백 안에서 호스트로부터 가져옵니다.

```c
static NevercStatus NEVERC_CALL
observe_arguments(const NevercPhaseFrame *Frame, NevercObserverPoint Point,
                  void *UserData) {
  DriverTraceProcessState *Process = (DriverTraceProcessState *)UserData;
  DriverTraceSessionState *Session = NULL;
  uint64_t ArgumentCount = 0;
  NevercStatus Status;
  if (Frame == NULL || Process == NULL)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  Status = Process->Core->GetSessionState(Process->Core->Context,
                                          Frame->Session, plugin_id(),
                                          (void **)&Session);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = Process->Driver->GetArgumentCount(Process->Driver->Context, Frame,
                                             Frame->Input, &ArgumentCount);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  ++Session->ArgumentCallbacks;
  if (Point == NEVERC_OBSERVER_BEFORE && !Session->Announced) {
    Session->Announced = NEVERC_TRUE;
    return emit_trace_remark(Process, Frame, "driver argument phase observed",
                             30, 1001);
  }
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL
intercept_job(const NevercPhaseFrame *Frame,
              NevercPhaseContinuation *Continuation,
              NevercPhaseResult *OutResult, void *UserData) {
  DriverTraceProcessState *Process = (DriverTraceProcessState *)UserData;
  NevercJobExecutionRequest Request = {0};
  NevercPhaseResult Downstream = {0};
  NevercStatus Status;
  if (Frame == NULL || Continuation == NULL || OutResult == NULL || !Process)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  Request.Header = (NevercABITableHeader){
      sizeof(Request), NEVERC_DRIVER_API_MAJOR, NEVERC_DRIVER_API_MINOR, 0};
  Status = Process->Driver->GetJobExecutionRequest(
      Process->Driver->Context, Frame, Frame->Input, &Request);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  Downstream.Header = (NevercABITableHeader){
      sizeof(Downstream), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Status = Continuation->InvokeNext(Continuation, Frame, &Downstream);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  *OutResult = (NevercPhaseResult){0};
  OutResult->Header = (NevercABITableHeader){
      sizeof(*OutResult), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  OutResult->Action = NEVERC_PHASE_CONTINUE;
  return neverc_status_ok();
}
```

등록에서 둘을 각자의 단계에 연결합니다:

```c
Observer.Phase = phase_id(NEVERC_PHASE_DRIVER_RAW_ARGUMENTS_HIGH,
                          NEVERC_PHASE_DRIVER_RAW_ARGUMENTS_LOW);
Observer.Points = NEVERC_OBSERVER_BEFORE | NEVERC_OBSERVER_AFTER;
Observer.Callback = observe_arguments;
Observer.UserData = Process;
Registrar->RegisterObserver(RegistrarContext, &Observer);

Interceptor.Phase = phase_id(NEVERC_PHASE_DRIVER_EXECUTE_JOB_HIGH,
                             NEVERC_PHASE_DRIVER_EXECUTE_JOB_LOW);
Interceptor.Callback = intercept_job;
Interceptor.UserData = Process;
Registrar->RegisterInterceptor(RegistrarContext, &Interceptor);
```

빌드하고 실행:

```sh
cmake --build build-neverc --target neverc-plugin-example-driver-trace
build-neverc/bin/neverc \
  -fplugin=build-neverc/neverc/pluginsdk/examples/host/DriverTracePlugin.so \
  --driver-trace -c input.c -o input.o
```

## 규칙

- 인자, 파싱된 인자, 툴체인, 액션 그래프, 작업 그래프의 변경은 모두 인터셉터의
  `NevercPhaseContinuation` 을 필요로 합니다. 그 밖에서는
  `NEVERC_STATUS_WRONG_SCOPE` 로 거부됩니다.
- `InvokeNext` 는 많아야 한 번, 콜백 스레드에서만 호출하세요.
- 모든 변경 핸들은 정확히 한 번의 `Commit*` 또는 `Abort*` 에 도달해야 합니다.
- `Get*` 호출이 돌려주는 뷰는 콜백 동안만 빌린 것입니다. 보관할 것은 복사하세요.
- `NEVERC_JOB_PLUGIN` 콜백이 호스트가 띄웠을 프로세스를 직접 spawn 하면서 동시에
  내장 경로의 성공까지 보고해서는 안 됩니다. `REPLACE` 를 선언하고 결과를 스스로
  책임지세요.
- 실제로 실행되어 정당하게 실패한 작업은 OK 가 아닌 상태를 반환하지 말고
  `NevercJobResultDescriptor.ExecutionFailed` 와 `ErrorMessage` 로 보고하세요.

규범적 선언은 [`PluginDriver.h`], 드라이버 단계 정책은 [`PhaseSchema.json`], 테스트
근거는 [`coverage.json`] 을 참고하세요.

<!-- reference links -->
[`coverage.json`]: coverage.json
[`PhaseSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PhaseSchema.json
[`PluginDriver.h`]: ../../neverc/include/neverc/Plugin/PluginDriver.h
[`pluginsdk/examples/DriverTracePlugin.c`]: ../../pluginsdk/examples/DriverTracePlugin.c
