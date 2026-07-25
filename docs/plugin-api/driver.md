**Languages**: [English](driver.md) | [简体中文](driver.zh-CN.md) | [繁體中文](driver.zh-TW.md) | [日本語](driver.ja.md) | [한국어](driver.ko.md) | [Français](driver.fr.md) | [Deutsch](driver.de.md) | [Español](driver.es.md) | [Italiano](driver.it.md) | [Русский](driver.ru.md) | [العربية](driver.ar.md)

# NeverC Plugin Driver API

The driver turns a command line into a set of executed jobs. `PluginDriver.h`
exposes that pipeline as six phases and one capability table,
`NevercDriverAPI`, so a plugin can rewrite arguments, choose a toolchain,
restructure the action graph, add or replace jobs, and even run a job
in-process instead of spawning a process.

## Interface

```c
#include "neverc/Plugin/PluginDriver.h"

Bootstrap->QueryInterface(
    Bootstrap->Context,
    (NevercInterfaceID){NEVERC_INTERFACE_DRIVER_HIGH,
                        NEVERC_INTERFACE_DRIVER_LOW},
    NEVERC_DRIVER_API_MAJOR, NEVERC_DRIVER_API_MINOR,
    &Table, &Minor, &TableSize);
```

`NevercDriverAPI` is one flat table of 67 function slots grouped into five
areas: raw arguments, parsed options, toolchain selection, the action graph,
and the job graph. Validate `TableSize` against the offset of the last slot
you use — `GetJobResult` is the current tail.

## The six driver phases

| Phase | Policy | Input → output |
|---|---|---|
| `neverc.driver.raw_arguments` | OBSERVABLE, INTERCEPTABLE | argv → argv |
| `neverc.driver.parsed_arguments` | OBSERVABLE, INTERCEPTABLE | parsed option list → parsed option list |
| `neverc.driver.select_toolchain` | + REPLACEABLE | toolchain request → toolchain selection |
| `neverc.driver.build_actions` | + REPLACEABLE | request → action graph |
| `neverc.driver.build_jobs` | + REPLACEABLE | action graph → job graph |
| `neverc.driver.execute_job` | + REPLACEABLE | job execution request → job result |

Their macros follow the usual pattern:
`NEVERC_PHASE_DRIVER_RAW_ARGUMENTS_{NAME,HIGH,LOW,POLICY,…}`.

## Registering an option

Options are declared once, during `Register`, and the driver then accepts them
on the command line exactly as if they were built in.

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

From `pluginsdk/examples/DriverTracePlugin.c`:

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

`Validator` is called per occurrence with a `NevercOptionValidationContext`
carrying the plugin ID, spelling, target triple, and occurrence index, so a
value can be rejected with a real diagnostic instead of failing later.
`TargetPredicate` restricts an option to matching triples. Read values back
with `NevercCoreAPI.GetPluginOptionValueCount` and `GetPluginOptionValue`.

## Raw arguments

At `neverc.driver.raw_arguments` the artifact is the argv vector. Reading is
index-based, and each entry reports where it came from:

```c
Driver->GetArgumentCount(Driver->Context, Frame, Frame->Input, &Count);

NevercStringView Value, Source;
NevercArgumentOrigin Origin;   /* COMMAND_LINE, CONFIGURATION, PLUGIN */
uint64_t Position;
Driver->GetArgument(Driver->Context, Frame, Frame->Input, Index,
                    &Value, &Origin, &Source, &Position);
```

Editing is transactional and only legal from an interceptor, because the
mutation is bound to the continuation:

```c
NevercArgumentMutationHandle Mutation;
Driver->BeginArgumentMutation(Driver->Context, Frame, Continuation,
                              Frame->Input, &Mutation);
Driver->InsertArgument(Driver->Context, Mutation, Index, SV("-O2"));
Driver->ReplaceArgument(Driver->Context, Mutation, Index, SV("-O3"));
Driver->EraseArgument(Driver->Context, Mutation, Index);
Driver->CommitArgumentMutation(Driver->Context, Mutation);  /* or Abort */
```

## Parsed arguments

`neverc.driver.parsed_arguments` works on option occurrences rather than
strings, which is what you want when adding a flag that must not be
re-lexed:

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

`GetOptionOccurrenceCount` and `GetOptionOccurrence` read; then
`BeginParsedArgumentMutation`, `AddOptionOccurrence`,
`RemoveOptionOccurrence`, `ReplaceOptionOccurrence`, and
`CommitParsedArgumentMutation` / `AbortParsedArgumentMutation` edit.

## Toolchain selection

The request describes what was asked for and what the driver computed:

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

An interceptor can nudge the request with `BeginToolChainMutation`,
`SetToolChainTriple`, `SetToolChainCPU`, `SetToolChainFeatures`, and
`CommitToolChainMutation`. A provider instead answers the phase outright with
`CreateToolChainSelection`, naming one of the built-in toolchain IDs or its
own:

```c
NEVERC_TOOLCHAIN_ID_DARWIN        /* "neverc.builtin.darwin"      */
NEVERC_TOOLCHAIN_ID_LINUX         /* "neverc.builtin.linux"       */
NEVERC_TOOLCHAIN_ID_MSVC          /* "neverc.builtin.msvc"        */
NEVERC_TOOLCHAIN_ID_GENERIC_ELF   /* "neverc.builtin.generic-elf" */
NEVERC_TOOLCHAIN_ID_MACHO         /* "neverc.builtin.macho"       */
NEVERC_TOOLCHAIN_ID_GENERIC_GCC   /* "neverc.builtin.generic-gcc" */
```

`GetToolChainSelection` reads the result back and reports
`BuiltinProviderUsed`, so an observer can tell whether a plugin won the phase.

## The action graph

An action node is a typed compilation step. Nodes reference driver inputs and
other nodes:

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

Read with `GetDriverInputCount` / `GetDriverInput`, `GetActionNodeCount` /
`GetActionNode` / `GetActionNodeInput`, and `GetActionRootCount` /
`GetActionRoot`.

Building a replacement graph goes through a builder, then a single publish:

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

`RemoveActionNode`, `ReplaceActionNodeInputs`, `SetActionNodeOutputType`, and
`SetActionNodeBindArch` edit an in-progress builder. To adjust the host's
existing graph instead of rebuilding it, use `BeginActionGraphMutation` and
`CommitActionGraphMutation`; `AbortActionGraphEdit` discards either form.

## The job graph

A job is a command to run. `NevercJobDescriptor` describes one:

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

Set `Kind` to `NEVERC_JOB_PLUGIN` with a `Callback` and the driver runs your
function where it would otherwise spawn a process:

```c
static NevercStatus NEVERC_CALL run_job(const NevercPluginJobContext *Context,
                                        int32_t *OutExitCode, void *UserData) {
  /* Context->Arguments, ->Environment, ->Inputs, ->Outputs are borrowed. */
  *OutExitCode = 0;
  return neverc_status_ok();
}
```

Reading the graph mirrors the action graph: `GetJobCount` / `GetJob`,
`GetJobDependency`, `GetJobArgument` / `GetJobEnvironment`, `GetJobInput` /
`GetJobOutput`. Note that `NevercJob` reports only counts — fetch each string
or file by index rather than expecting an inline array.

Editing uses `CreateJobGraphBuilder` or `BeginJobGraphMutation`, then
`AddJob`, `RemoveJob`, `MoveJobBefore`, `ReplaceJob`, `SetJobArgument`,
`SetJobEnvironment`, `SetJobInput`, `SetJobOutput`, and
`ReplaceJobDependencies`. Publish with `PublishJobGraph` or
`CommitJobGraphMutation`; discard with `AbortJobGraphEdit`.

## Executing a job

At `neverc.driver.execute_job` the input artifact is a
`NevercJobExecutionRequest` — the job plus its fully materialized argument,
environment, input, output, and dependency lists. A provider runs the job and
reports the result:

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

`OutputSeals` carries the `NevercOutputSealHandle`s produced through the I/O
API (see [Source and I/O](source.md)), which is how the host confirms that the
files a job claimed to write actually exist with the digests reported.
`GetJobResult` reads a committed result and, like the toolchain selection,
reports `BuiltinProviderUsed`.

## Worked example: observe arguments, intercept job execution

Condensed from `pluginsdk/examples/DriverTracePlugin.c`. The plugin keeps no
globals: process state holds the negotiated tables, and per-session and
per-task counters are fetched from the host inside each callback.

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

Registration wires both to their phases:

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

Build and run it:

```sh
cmake --build build-neverc --target neverc-plugin-example-driver-trace
build-neverc/bin/neverc \
  -fplugin=build-neverc/neverc/pluginsdk/examples/host/DriverTracePlugin.so \
  --driver-trace -c input.c -o input.o
```

## Rules

- Argument, parsed-argument, toolchain, action-graph, and job-graph mutations
  all require the interceptor's `NevercPhaseContinuation`; they are rejected
  with `NEVERC_STATUS_WRONG_SCOPE` outside one.
- Call `InvokeNext` at most once and only on the callback thread.
- Every mutation handle must reach exactly one `Commit*` or `Abort*`.
- Views returned by a `Get*` call are borrowed for the callback. Copy anything
  you need to keep.
- A `NEVERC_JOB_PLUGIN` callback must not spawn the process the host would
  have spawned and then also report success for the built-in path; declare
  `REPLACE` and own the outcome.
- Report a failed job through `NevercJobResultDescriptor.ExecutionFailed` and
  `ErrorMessage` rather than returning a non-OK status for a job that ran and
  legitimately failed.

See `PluginDriver.h` for the normative declarations, `PhaseSchema.json` for
the driver phase policies, and `coverage.json` for the test evidence.
