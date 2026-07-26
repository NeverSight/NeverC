**言語**: [English](driver.md) | [简体中文](driver.zh-CN.md) | [繁體中文](driver.zh-TW.md) | [日本語](driver.ja.md) | [한국어](driver.ko.md) | [Français](driver.fr.md) | [Deutsch](driver.de.md) | [Español](driver.es.md) | [Italiano](driver.it.md) | [Русский](driver.ru.md) | [العربية](driver.ar.md)

[← NeverC プラグイン ABI](README.ja.md)

# NeverC プラグイン Driver API

ドライバはコマンドラインを実行されるジョブの集合へ変換します。
[`PluginDriver.h`] はそのパイプラインを 6 つのフェーズと 1 つのケーパビリティ
テーブル `NevercDriverAPI` として公開しており、プラグインは引数の書き換え、
ツールチェーンの選択、アクショングラフの再構成、ジョブの追加や置き換え、さらに
はプロセスを spawn せずにジョブをインプロセスで実行することまで行えます。

## インターフェース

```c
#include "neverc/Plugin/PluginDriver.h"

Bootstrap->QueryInterface(
    Bootstrap->Context,
    (NevercInterfaceID){NEVERC_INTERFACE_DRIVER_HIGH,
                        NEVERC_INTERFACE_DRIVER_LOW},
    NEVERC_DRIVER_API_MAJOR, NEVERC_DRIVER_API_MINOR,
    &Table, &Minor, &TableSize);
```

`NevercDriverAPI` は 67 個の関数スロットからなる 1 枚のフラットなテーブルで、
生の引数、パース済みオプション、ツールチェーン選択、アクショングラフ、ジョブ
グラフという 5 つの領域に分かれています。`TableSize` は自分が使う最後のスロット
のオフセットと突き合わせて検証してください。現在の末尾は `GetJobResult` です。

## 6 つのドライバフェーズ

| フェーズ | ポリシー | 入力 → 出力 |
|---|---|---|
| `neverc.driver.raw_arguments` | OBSERVABLE、INTERCEPTABLE | argv → argv |
| `neverc.driver.parsed_arguments` | OBSERVABLE、INTERCEPTABLE | パース済みオプション列 → パース済みオプション列 |
| `neverc.driver.select_toolchain` | さらに REPLACEABLE | ツールチェーン要求 → ツールチェーン選択 |
| `neverc.driver.build_actions` | さらに REPLACEABLE | 要求 → アクショングラフ |
| `neverc.driver.build_jobs` | さらに REPLACEABLE | アクショングラフ → ジョブグラフ |
| `neverc.driver.execute_job` | さらに REPLACEABLE | ジョブ実行要求 → ジョブ結果 |

対応するマクロはいつもの命名規則に従います:
`NEVERC_PHASE_DRIVER_RAW_ARGUMENTS_{NAME,HIGH,LOW,POLICY,…}`。

## オプションの登録

オプションは `Register` の間に一度だけ宣言します。以降ドライバは、それが組み
込みであるかのようにコマンドライン上で受け付けます。

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

[`pluginsdk/examples/DriverTracePlugin.c`] より:

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

`Validator` は出現ごとに呼ばれ、プラグイン ID、綴り、ターゲットトリプル、出現
インデックスを持つ `NevercOptionValidationContext` を受け取ります。これにより、
後段で失敗する代わりに本物の診断で値を拒否できます。`TargetPredicate` はオプ
ションを一致するトリプルに限定します。値の読み戻しには
`NevercCoreAPI.GetPluginOptionValueCount` と `GetPluginOptionValue` を使います。

## 生の引数

`neverc.driver.raw_arguments` では、アーティファクトは argv ベクタそのものです。
読み取りはインデックスベースで、各エントリはその出所を報告します:

```c
Driver->GetArgumentCount(Driver->Context, Frame, Frame->Input, &Count);

NevercStringView Value, Source;
NevercArgumentOrigin Origin;   /* COMMAND_LINE, CONFIGURATION, PLUGIN */
uint64_t Position;
Driver->GetArgument(Driver->Context, Frame, Frame->Input, Index,
                    &Value, &Origin, &Source, &Position);
```

編集はトランザクショナルで、インターセプタの内部からのみ合法です。ミューテー
ションが continuation に束縛されるためです:

```c
NevercArgumentMutationHandle Mutation;
Driver->BeginArgumentMutation(Driver->Context, Frame, Continuation,
                              Frame->Input, &Mutation);
Driver->InsertArgument(Driver->Context, Mutation, Index, SV("-O2"));
Driver->ReplaceArgument(Driver->Context, Mutation, Index, SV("-O3"));
Driver->EraseArgument(Driver->Context, Mutation, Index);
Driver->CommitArgumentMutation(Driver->Context, Mutation);  /* または Abort */
```

## パース済み引数

`neverc.driver.parsed_arguments` は文字列ではなくオプションの出現単位で動作し
ます。再字句解析されては困るフラグを追加したいときに必要になるのがこの層です:

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

読み取りは `GetOptionOccurrenceCount` と `GetOptionOccurrence`、編集は
`BeginParsedArgumentMutation`、`AddOptionOccurrence`、
`RemoveOptionOccurrence`、`ReplaceOptionOccurrence`、そして
`CommitParsedArgumentMutation` / `AbortParsedArgumentMutation` です。

## ツールチェーンの選択

要求は「何が指定されたか」と「ドライバが何を計算したか」の両方を記述します:

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

インターセプタは `BeginToolChainMutation`、`SetToolChainTriple`、
`SetToolChainCPU`、`SetToolChainFeatures`、`CommitToolChainMutation` で要求を
調整できます。一方プロバイダは `CreateToolChainSelection` でフェーズそのものに
答え、組み込みツールチェーン ID か自前の ID を指名します:

```c
NEVERC_TOOLCHAIN_ID_DARWIN        /* "neverc.builtin.darwin"      */
NEVERC_TOOLCHAIN_ID_LINUX         /* "neverc.builtin.linux"       */
NEVERC_TOOLCHAIN_ID_MSVC          /* "neverc.builtin.msvc"        */
NEVERC_TOOLCHAIN_ID_GENERIC_ELF   /* "neverc.builtin.generic-elf" */
NEVERC_TOOLCHAIN_ID_MACHO         /* "neverc.builtin.macho"       */
NEVERC_TOOLCHAIN_ID_GENERIC_GCC   /* "neverc.builtin.generic-gcc" */
```

`GetToolChainSelection` は結果を読み戻し、`BuiltinProviderUsed` を報告します。
オブザーバはこれによって、そのフェーズをプラグインが取ったかどうかを判別でき
ます。

## アクショングラフ

アクションノードは型付きのコンパイル手順です。ノードはドライバ入力や他のノード
を参照します:

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

読み取りは `GetDriverInputCount` / `GetDriverInput`、`GetActionNodeCount` /
`GetActionNode` / `GetActionNodeInput`、`GetActionRootCount` /
`GetActionRoot` を使います。

差し替えグラフの構築はビルダを経由し、最後に一度だけ publish します:

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

`RemoveActionNode`、`ReplaceActionNodeInputs`、`SetActionNodeOutputType`、
`SetActionNodeBindArch` は作成中のビルダを編集します。再構築するのではなく
ホスト側の既存グラフを調整したい場合は `BeginActionGraphMutation` と
`CommitActionGraphMutation` を使ってください。どちらの形式も
`AbortActionGraphEdit` で破棄できます。

## ジョブグラフ

ジョブは実行されるコマンドです。`NevercJobDescriptor` がその 1 つを記述します:

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

`Kind` を `NEVERC_JOB_PLUGIN` にして `Callback` を設定すると、ドライバは本来
プロセスを spawn する場所であなたの関数を実行します:

```c
static NevercStatus NEVERC_CALL run_job(const NevercPluginJobContext *Context,
                                        int32_t *OutExitCode, void *UserData) {
  /* Context->Arguments、->Environment、->Inputs、->Outputs は借用です。 */
  *OutExitCode = 0;
  return neverc_status_ok();
}
```

グラフの読み取りはアクショングラフと同じ形です: `GetJobCount` / `GetJob`、
`GetJobDependency`、`GetJobArgument` / `GetJobEnvironment`、`GetJobInput` /
`GetJobOutput`。`NevercJob` が報告するのは個数だけである点に注意してください。
インライン配列を期待せず、文字列やファイルはインデックスで取得します。

編集は `CreateJobGraphBuilder` か `BeginJobGraphMutation` から始め、`AddJob`、
`RemoveJob`、`MoveJobBefore`、`ReplaceJob`、`SetJobArgument`、
`SetJobEnvironment`、`SetJobInput`、`SetJobOutput`、
`ReplaceJobDependencies` を使います。公開は `PublishJobGraph` または
`CommitJobGraphMutation`、破棄は `AbortJobGraphEdit` です。

## ジョブの実行

`neverc.driver.execute_job` では、入力アーティファクトは
`NevercJobExecutionRequest` です。すなわちジョブ本体に加えて、完全に実体化され
た引数・環境・入力・出力・依存のリストが含まれます。プロバイダはジョブを実行し
結果を報告します:

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

`OutputSeals` は I/O API 経由で生成された `NevercOutputSealHandle` を運びます
（[Source と I/O](source.ja.md#出力の書き出し) を参照）。ホストはこれによって、
ジョブが書いたと主張したファイルが報告どおりのダイジェストで実在することを
確認します。
`GetJobResult` はコミット済みの結果を読み、ツールチェーン選択と同様に
`BuiltinProviderUsed` を報告します。

## 実例: 引数を観察し、ジョブ実行をインターセプトする

[`pluginsdk/examples/DriverTracePlugin.c`] を圧縮したものです。このプラグインは
グローバル変数を一切持ちません。プロセス状態がネゴシエート済みテーブルを保持
し、セッション単位・タスク単位のカウンタは各コールバック内でホストから取得し
ます。

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

登録で両者をそれぞれのフェーズへ接続します:

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

ビルドして実行:

```sh
cmake --build build-neverc --target neverc-plugin-example-driver-trace
build-neverc/bin/neverc \
  -fplugin=build-neverc/neverc/pluginsdk/examples/host/DriverTracePlugin.so \
  --driver-trace -c input.c -o input.o
```

## ルール

- 引数、パース済み引数、ツールチェーン、アクショングラフ、ジョブグラフの
  ミューテーションはすべてインターセプタの `NevercPhaseContinuation` を必要と
  します。その外では `NEVERC_STATUS_WRONG_SCOPE` で拒否されます。
- `InvokeNext` は高々 1 回、コールバックスレッド上でのみ呼びます。
- すべてのミューテーションハンドルは、ちょうど 1 回の `Commit*` か `Abort*` に
  到達しなければなりません。
- `Get*` が返すビューはコールバックの間だけ借用されています。保持したいものは
  コピーしてください。
- `NEVERC_JOB_PLUGIN` のコールバックが、ホストが起動したはずのプロセスを自分で
  spawn しておきながら組み込み経路の成功も報告する、という振る舞いをしては
  なりません。`REPLACE` を宣言し、結果に責任を持ってください。
- 実際に実行され、正当に失敗したジョブは、非 OK ステータスを返すのではなく
  `NevercJobResultDescriptor.ExecutionFailed` と `ErrorMessage` で報告して
  ください。

規範的な宣言は [`PluginDriver.h`]、ドライバフェーズのポリシーは
[`PhaseSchema.json`]、テストの証跡は [`coverage.json`] を参照してください。

<!-- reference links -->
[`coverage.json`]: coverage.json
[`PhaseSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PhaseSchema.json
[`PluginDriver.h`]: ../../neverc/include/neverc/Plugin/PluginDriver.h
[`pluginsdk/examples/DriverTracePlugin.c`]: ../../pluginsdk/examples/DriverTracePlugin.c
