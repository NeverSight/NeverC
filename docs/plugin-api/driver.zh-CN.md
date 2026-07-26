**语言**: [English](driver.md) | [简体中文](driver.zh-CN.md) | [繁體中文](driver.zh-TW.md) | [日本語](driver.ja.md) | [한국어](driver.ko.md) | [Français](driver.fr.md) | [Deutsch](driver.de.md) | [Español](driver.es.md) | [Italiano](driver.it.md) | [Русский](driver.ru.md) | [العربية](driver.ar.md)

[← NeverC 插件 ABI](README.zh-CN.md)

# NeverC 插件驱动 API

驱动把一条命令行变成一组被执行的 job。[`PluginDriver.h`] 把这条流水线暴露为六个
阶段和一张能力表 `NevercDriverAPI`，于是插件可以改写参数、选择工具链、重构
action 图、增加或替换 job，甚至在进程内直接运行某个 job 而不是派生一个进程。

## 接口

```c
#include "neverc/Plugin/PluginDriver.h"

Bootstrap->QueryInterface(
    Bootstrap->Context,
    (NevercInterfaceID){NEVERC_INTERFACE_DRIVER_HIGH,
                        NEVERC_INTERFACE_DRIVER_LOW},
    NEVERC_DRIVER_API_MAJOR, NEVERC_DRIVER_API_MINOR,
    &Table, &Minor, &TableSize);
```

`NevercDriverAPI` 是一张扁平的表，共 67 个函数槽位，分成五个区域：原始参数、已
解析选项、工具链选择、action 图、job 图。用你要调用的最后一个槽位的偏移去校验
`TableSize`——目前表尾是 `GetJobResult`。

## 六个驱动阶段

| 阶段 | 策略 | 输入 → 输出 |
|---|---|---|
| `neverc.driver.raw_arguments` | OBSERVABLE, INTERCEPTABLE | argv → argv |
| `neverc.driver.parsed_arguments` | OBSERVABLE, INTERCEPTABLE | 已解析选项列表 → 已解析选项列表 |
| `neverc.driver.select_toolchain` | 另加 REPLACEABLE | 工具链请求 → 工具链选择 |
| `neverc.driver.build_actions` | 另加 REPLACEABLE | 请求 → action 图 |
| `neverc.driver.build_jobs` | 另加 REPLACEABLE | action 图 → job 图 |
| `neverc.driver.execute_job` | 另加 REPLACEABLE | job 执行请求 → job 结果 |

它们的宏遵循惯例：
`NEVERC_PHASE_DRIVER_RAW_ARGUMENTS_{NAME,HIGH,LOW,POLICY,…}`。

## 注册选项

选项只在 `Register` 期间声明一次，此后驱动接受它的方式与内建选项完全一样。

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

摘自 [`pluginsdk/examples/DriverTracePlugin.c`]：

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

`Validator` 对每次出现都会被调用一次，并带上一个
`NevercOptionValidationContext`，其中有插件 ID、拼写、目标 triple 和本次出现的
序号，因此可以用一条真正的诊断拒绝某个取值，而不是等到后面才失败。
`TargetPredicate` 把选项限制在匹配的 triple 上。用
`NevercCoreAPI.GetPluginOptionValueCount` 和 `GetPluginOptionValue` 读回取值。

## 原始参数

在 `neverc.driver.raw_arguments` 处，artifact 就是 argv 向量。读取按下标进行，
每一项都会报告它的来源：

```c
Driver->GetArgumentCount(Driver->Context, Frame, Frame->Input, &Count);

NevercStringView Value, Source;
NevercArgumentOrigin Origin;   /* COMMAND_LINE, CONFIGURATION, PLUGIN */
uint64_t Position;
Driver->GetArgument(Driver->Context, Frame, Frame->Input, Index,
                    &Value, &Origin, &Source, &Position);
```

编辑是事务式的，而且只能在拦截器里做，因为 mutation 绑定在 continuation 上：

```c
NevercArgumentMutationHandle Mutation;
Driver->BeginArgumentMutation(Driver->Context, Frame, Continuation,
                              Frame->Input, &Mutation);
Driver->InsertArgument(Driver->Context, Mutation, Index, SV("-O2"));
Driver->ReplaceArgument(Driver->Context, Mutation, Index, SV("-O3"));
Driver->EraseArgument(Driver->Context, Mutation, Index);
Driver->CommitArgumentMutation(Driver->Context, Mutation);  /* 或 Abort */
```

## 已解析参数

`neverc.driver.parsed_arguments` 处理的是选项出现（occurrence）而不是字符串，
当你要添加一个不该被重新词法分析的 flag 时，这正是你想要的：

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

`GetOptionOccurrenceCount` 和 `GetOptionOccurrence` 负责读；
`BeginParsedArgumentMutation`、`AddOptionOccurrence`、
`RemoveOptionOccurrence`、`ReplaceOptionOccurrence` 以及
`CommitParsedArgumentMutation` / `AbortParsedArgumentMutation` 负责改。

## 工具链选择

请求同时描述了用户要求的和驱动算出来的：

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

拦截器可以用 `BeginToolChainMutation`、`SetToolChainTriple`、
`SetToolChainCPU`、`SetToolChainFeatures` 和 `CommitToolChainMutation` 微调请
求。Provider 则直接用 `CreateToolChainSelection` 回答这个阶段，指名某个内建工具
链 ID 或它自己的：

```c
NEVERC_TOOLCHAIN_ID_DARWIN        /* "neverc.builtin.darwin"      */
NEVERC_TOOLCHAIN_ID_LINUX         /* "neverc.builtin.linux"       */
NEVERC_TOOLCHAIN_ID_MSVC          /* "neverc.builtin.msvc"        */
NEVERC_TOOLCHAIN_ID_GENERIC_ELF   /* "neverc.builtin.generic-elf" */
NEVERC_TOOLCHAIN_ID_MACHO         /* "neverc.builtin.macho"       */
NEVERC_TOOLCHAIN_ID_GENERIC_GCC   /* "neverc.builtin.generic-gcc" */
```

`GetToolChainSelection` 读回结果，并报告 `BuiltinProviderUsed`，观察者据此就能
知道是否有插件赢下了这个阶段。

## Action 图

一个 action 节点是一个带类型的编译步骤。节点引用驱动输入和其他节点：

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

读取用 `GetDriverInputCount` / `GetDriverInput`、`GetActionNodeCount` /
`GetActionNode` / `GetActionNodeInput`，以及 `GetActionRootCount` /
`GetActionRoot`。

构造一张替代图要经过 builder，然后一次性发布：

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

`RemoveActionNode`、`ReplaceActionNodeInputs`、`SetActionNodeOutputType` 和
`SetActionNodeBindArch` 用来编辑构造中的 builder。如果想调整宿主已有的图而不是
重建，请用 `BeginActionGraphMutation` 和 `CommitActionGraphMutation`；两种形式都
可以用 `AbortActionGraphEdit` 丢弃。

## Job 图

一个 job 就是一条要运行的命令。`NevercJobDescriptor` 描述它：

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

把 `Kind` 设为 `NEVERC_JOB_PLUGIN` 并给出 `Callback`，驱动就会在本该派生进程的
地方运行你的函数：

```c
static NevercStatus NEVERC_CALL run_job(const NevercPluginJobContext *Context,
                                        int32_t *OutExitCode, void *UserData) {
  /* Context->Arguments、->Environment、->Inputs、->Outputs 都是借用的。 */
  *OutExitCode = 0;
  return neverc_status_ok();
}
```

读取 job 图与 action 图对称：`GetJobCount` / `GetJob`、`GetJobDependency`、
`GetJobArgument` / `GetJobEnvironment`、`GetJobInput` / `GetJobOutput`。注意
`NevercJob` 只报告计数——每个字符串或文件要按下标取，不要指望有内联数组。

编辑用 `CreateJobGraphBuilder` 或 `BeginJobGraphMutation`，然后是 `AddJob`、
`RemoveJob`、`MoveJobBefore`、`ReplaceJob`、`SetJobArgument`、
`SetJobEnvironment`、`SetJobInput`、`SetJobOutput`、
`ReplaceJobDependencies`。用 `PublishJobGraph` 或 `CommitJobGraphMutation` 发
布，用 `AbortJobGraphEdit` 丢弃。

## 执行一个 job

在 `neverc.driver.execute_job` 处，输入 artifact 是一个
`NevercJobExecutionRequest`——即这个 job 加上它已经完全实体化的参数、环境、输
入、输出和依赖列表。Provider 运行该 job 并报告结果：

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

`OutputSeals` 携带通过 I/O API 产生的 `NevercOutputSealHandle`（见
[源与 I/O](source.zh-CN.md#写输出)），宿主借此确认某个 job 声称写出的文件确实存在，且
摘要与报告一致。`GetJobResult` 读取一个已提交的结果，并且和工具链选择一样，会报
告 `BuiltinProviderUsed`。

## 完整示例：观察参数、拦截 job 执行

摘自 [`pluginsdk/examples/DriverTracePlugin.c`]。这个插件没有任何全局变量：进程状
态保存协商到的表，会话级和任务级计数器在每个回调里从宿主取。

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

注册时把两者接到各自的阶段上：

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

构建并运行它：

```sh
cmake --build build-neverc --target neverc-plugin-example-driver-trace
build-neverc/bin/neverc \
  -fplugin=build-neverc/neverc/pluginsdk/examples/host/DriverTracePlugin.so \
  --driver-trace -c input.c -o input.o
```

## 规则

- 参数、已解析参数、工具链、action 图和 job 图的修改都需要拦截器的
  `NevercPhaseContinuation`；在拦截器之外调用会被
  `NEVERC_STATUS_WRONG_SCOPE` 拒绝。
- `InvokeNext` 最多调用一次，且只能在回调线程上。
- 每个 mutation 句柄都必须恰好走到一次 `Commit*` 或 `Abort*`。
- `Get*` 返回的视图只在回调期间有效。需要留存的内容请自行复制。
- `NEVERC_JOB_PLUGIN` 回调不要既派生宿主本来会派生的进程、又为内建路径报告成
  功；声明 `REPLACE` 并对结果负全责。
- 一个真的运行了、但确实失败了的 job，应通过
  `NevercJobResultDescriptor.ExecutionFailed` 和 `ErrorMessage` 报告，而不是返
  回非 OK 状态。

规范性声明见 [`PluginDriver.h`]，驱动阶段策略见 [`PhaseSchema.json`]，测试证据见
[`coverage.json`]。

<!-- reference links -->
[`coverage.json`]: coverage.json
[`PhaseSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PhaseSchema.json
[`PluginDriver.h`]: ../../neverc/include/neverc/Plugin/PluginDriver.h
[`pluginsdk/examples/DriverTracePlugin.c`]: ../../pluginsdk/examples/DriverTracePlugin.c
