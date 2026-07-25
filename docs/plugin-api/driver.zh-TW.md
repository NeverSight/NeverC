**語言**: [English](driver.md) | [简体中文](driver.zh-CN.md) | [繁體中文](driver.zh-TW.md) | [日本語](driver.ja.md) | [한국어](driver.ko.md) | [Français](driver.fr.md) | [Deutsch](driver.de.md) | [Español](driver.es.md) | [Italiano](driver.it.md) | [Русский](driver.ru.md) | [العربية](driver.ar.md)

[← NeverC 外掛 ABI](README.zh-TW.md)

# NeverC 外掛 Driver API

Driver 把一條命令列轉換成一組實際執行的 job。`PluginDriver.h` 將這條管線公開為
六個階段與一張能力表 `NevercDriverAPI`，讓外掛可以改寫引數、選擇工具鏈、重組
action 圖、新增或替換 job，甚至在行程內執行某個 job 而不另外 spawn 一個行程。

## 介面

```c
#include "neverc/Plugin/PluginDriver.h"

Bootstrap->QueryInterface(
    Bootstrap->Context,
    (NevercInterfaceID){NEVERC_INTERFACE_DRIVER_HIGH,
                        NEVERC_INTERFACE_DRIVER_LOW},
    NEVERC_DRIVER_API_MAJOR, NEVERC_DRIVER_API_MINOR,
    &Table, &Minor, &TableSize);
```

`NevercDriverAPI` 是一張扁平的表，共 67 個函式槽位，分為五個區域：原始引數、
已解析選項、工具鏈選擇、action 圖與 job 圖。請以你會用到的最後一個槽位的
offset 來驗證 `TableSize`——目前的尾端是 `GetJobResult`。

## 六個 driver 階段

| 階段 | Policy | 輸入 → 輸出 |
|---|---|---|
| `neverc.driver.raw_arguments` | OBSERVABLE、INTERCEPTABLE | argv → argv |
| `neverc.driver.parsed_arguments` | OBSERVABLE、INTERCEPTABLE | 已解析選項清單 → 已解析選項清單 |
| `neverc.driver.select_toolchain` | 再加上 REPLACEABLE | 工具鏈請求 → 工具鏈選擇 |
| `neverc.driver.build_actions` | 再加上 REPLACEABLE | 請求 → action 圖 |
| `neverc.driver.build_jobs` | 再加上 REPLACEABLE | action 圖 → job 圖 |
| `neverc.driver.execute_job` | 再加上 REPLACEABLE | job 執行請求 → job 結果 |

它們的巨集遵循一貫的樣式：
`NEVERC_PHASE_DRIVER_RAW_ARGUMENTS_{NAME,HIGH,LOW,POLICY,…}`。

## 註冊選項

選項只在 `Register` 期間宣告一次，之後 driver 就會在命令列上接受它們，行為與
內建選項完全相同。

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

取自 `pluginsdk/examples/DriverTracePlugin.c`：

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

`Validator` 會針對每次出現被呼叫一次，並帶有一個
`NevercOptionValidationContext`，其中含有外掛 ID、拼法、目標 triple 與出現序
號，因此可以用真正的診斷拒絕某個值，而不是拖到後面才失敗。`TargetPredicate`
可將選項限制在符合條件的 triple 上。讀回值請用
`NevercCoreAPI.GetPluginOptionValueCount` 與 `GetPluginOptionValue`。

## 原始引數

在 `neverc.driver.raw_arguments`，產物就是 argv 向量。讀取以索引為基礎，且每個
項目都會回報它的來源：

```c
Driver->GetArgumentCount(Driver->Context, Frame, Frame->Input, &Count);

NevercStringView Value, Source;
NevercArgumentOrigin Origin;   /* COMMAND_LINE, CONFIGURATION, PLUGIN */
uint64_t Position;
Driver->GetArgument(Driver->Context, Frame, Frame->Input, Index,
                    &Value, &Origin, &Source, &Position);
```

編輯是交易式的，而且只有在攔截器內部才合法，因為該變更被繫結到 continuation
上：

```c
NevercArgumentMutationHandle Mutation;
Driver->BeginArgumentMutation(Driver->Context, Frame, Continuation,
                              Frame->Input, &Mutation);
Driver->InsertArgument(Driver->Context, Mutation, Index, SV("-O2"));
Driver->ReplaceArgument(Driver->Context, Mutation, Index, SV("-O3"));
Driver->EraseArgument(Driver->Context, Mutation, Index);
Driver->CommitArgumentMutation(Driver->Context, Mutation);  /* 或 Abort */
```

## 已解析引數

`neverc.driver.parsed_arguments` 處理的是選項的「出現」而非字串，當你要加入一個
不應被重新詞法分析的旗標時，這正是你需要的層級：

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

讀取用 `GetOptionOccurrenceCount` 與 `GetOptionOccurrence`；編輯則用
`BeginParsedArgumentMutation`、`AddOptionOccurrence`、
`RemoveOptionOccurrence`、`ReplaceOptionOccurrence`，最後
`CommitParsedArgumentMutation` 或 `AbortParsedArgumentMutation`。

## 工具鏈選擇

請求描述了使用者要求什麼、以及 driver 算出了什麼：

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

攔截器可以用 `BeginToolChainMutation`、`SetToolChainTriple`、
`SetToolChainCPU`、`SetToolChainFeatures` 與 `CommitToolChainMutation` 微調請
求。Provider 則直接以 `CreateToolChainSelection` 回答整個階段，指名某個內建工
具鏈 ID 或自己的 ID：

```c
NEVERC_TOOLCHAIN_ID_DARWIN        /* "neverc.builtin.darwin"      */
NEVERC_TOOLCHAIN_ID_LINUX         /* "neverc.builtin.linux"       */
NEVERC_TOOLCHAIN_ID_MSVC          /* "neverc.builtin.msvc"        */
NEVERC_TOOLCHAIN_ID_GENERIC_ELF   /* "neverc.builtin.generic-elf" */
NEVERC_TOOLCHAIN_ID_MACHO         /* "neverc.builtin.macho"       */
NEVERC_TOOLCHAIN_ID_GENERIC_GCC   /* "neverc.builtin.generic-gcc" */
```

`GetToolChainSelection` 讀回結果，並回報 `BuiltinProviderUsed`，因此觀察者可以
判斷該階段是否被某個外掛拿下。

## Action 圖

Action 節點是一個具型別的編譯步驟。節點可以引用 driver 輸入與其他節點：

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
| `INPUT` | `BIND_ARCH` | `PP_C`、`C`、`C_HEADER` | `PP_ASM`、`ASM` |
| `PREPROCESS` | `COMPILE` | `LLVM_IR`、`LLVM_BC` | `LTO_IR`、`LTO_BC` |
| `BACKEND` | `ASSEMBLE` | `OBJECT`、`IMAGE` | `DSYM` |
| `LINK`、`LIPO` | `DSYMUTIL` | `DEPENDENCIES` | `NOTHING` |
| `STATIC_LIB` | `DYNCODE` | | |

讀取請用 `GetDriverInputCount` / `GetDriverInput`、`GetActionNodeCount` /
`GetActionNode` / `GetActionNodeInput`，以及 `GetActionRootCount` /
`GetActionRoot`。

要建構一張替代的圖，需經過 builder，最後一次發布：

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

`RemoveActionNode`、`ReplaceActionNodeInputs`、`SetActionNodeOutputType` 與
`SetActionNodeBindArch` 可編輯進行中的 builder。若想調整宿主既有的圖而非重建，
請改用 `BeginActionGraphMutation` 與 `CommitActionGraphMutation`；兩種形式都可
用 `AbortActionGraphEdit` 丟棄。

## Job 圖

Job 就是一條要執行的命令。`NevercJobDescriptor` 描述其中一條：

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

把 `Kind` 設為 `NEVERC_JOB_PLUGIN` 並提供 `Callback`，driver 就會在原本要
spawn 行程的地方改為執行你的函式：

```c
static NevercStatus NEVERC_CALL run_job(const NevercPluginJobContext *Context,
                                        int32_t *OutExitCode, void *UserData) {
  /* Context->Arguments、->Environment、->Inputs、->Outputs 都是借用的。 */
  *OutExitCode = 0;
  return neverc_status_ok();
}
```

讀取這張圖的方式與 action 圖相同：`GetJobCount` / `GetJob`、
`GetJobDependency`、`GetJobArgument` / `GetJobEnvironment`、`GetJobInput` /
`GetJobOutput`。請注意 `NevercJob` 只回報數量——每個字串或檔案要依索引取得，不
要預期會有內嵌陣列。

編輯則用 `CreateJobGraphBuilder` 或 `BeginJobGraphMutation`，接著是 `AddJob`、
`RemoveJob`、`MoveJobBefore`、`ReplaceJob`、`SetJobArgument`、
`SetJobEnvironment`、`SetJobInput`、`SetJobOutput` 與
`ReplaceJobDependencies`。以 `PublishJobGraph` 或 `CommitJobGraphMutation` 發
布；以 `AbortJobGraphEdit` 丟棄。

## 執行一個 job

在 `neverc.driver.execute_job`，輸入產物是一個 `NevercJobExecutionRequest`——
即該 job 加上它已完全具體化的引數、環境、輸入、輸出與相依清單。Provider 執行該
job 並回報結果：

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

`OutputSeals` 攜帶透過 I/O API 產生的 `NevercOutputSealHandle`（見
[Source 與 I/O](source.zh-TW.md)），宿主藉此確認某個 job 宣稱寫出的檔案確實存
在、且 digest 與回報的一致。`GetJobResult` 讀取已提交的結果，並且與工具鏈選擇
一樣會回報 `BuiltinProviderUsed`。

## 實例：觀察引數、攔截 job 執行

濃縮自 `pluginsdk/examples/DriverTracePlugin.c`。這個外掛完全不用全域變數：行程
狀態存放已協商的表，而 session 與 task 層級的計數器則在每個回呼內部向宿主取得。

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

註冊時把兩者接到各自的階段：

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

建置並執行：

```sh
cmake --build build-neverc --target neverc-plugin-example-driver-trace
build-neverc/bin/neverc \
  -fplugin=build-neverc/neverc/pluginsdk/examples/host/DriverTracePlugin.so \
  --driver-trace -c input.c -o input.o
```

## 規則

- 引數、已解析引數、工具鏈、action 圖與 job 圖的變更全都需要攔截器的
  `NevercPhaseContinuation`；在攔截器之外會以 `NEVERC_STATUS_WRONG_SCOPE` 被拒
  絕。
- `InvokeNext` 最多呼叫一次，且只能在回呼執行緒上呼叫。
- 每個 mutation handle 都必須恰好走到一個 `Commit*` 或 `Abort*`。
- `Get*` 呼叫回傳的視圖只在回呼期間借用。需要保留的內容請自行複製。
- `NEVERC_JOB_PLUGIN` 回呼不可以一邊 spawn 宿主原本會 spawn 的行程，一邊又回報
  內建路徑成功；請宣告 `REPLACE` 並自行承擔結果。
- 對於確實執行過、但正當地失敗的 job，請透過
  `NevercJobResultDescriptor.ExecutionFailed` 與 `ErrorMessage` 回報，而不是回
  傳非 OK 的 status。

規範宣告請見 `PluginDriver.h`，driver 階段的 policy 見 `PhaseSchema.json`，測試
證據見 `coverage.json`。
