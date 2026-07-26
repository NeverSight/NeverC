**言語**: [English](ir.md) | [简体中文](ir.zh-CN.md) | [繁體中文](ir.zh-TW.md) | [日本語](ir.ja.md) | [한국어](ir.ko.md) | [Français](ir.fr.md) | [Deutsch](ir.de.md) | [Español](ir.es.md) | [Italiano](ir.it.md) | [Русский](ir.ru.md) | [العربية](ir.ar.md)

[← NeverC プラグイン ABI](README.ja.md)

# NeverC プラグイン IR API

[`PluginIR.h`] は 6 枚のケーパビリティテーブルと生成されたスキーマを通じて LLVM IR
を公開します。プラグインは IR を読み書きし、5 つの安定したパイプライン地点に
パスを登録し、自前の解析を定義し、あるいは IR 生成と最適化パイプラインをまるごと
置き換えられます —— LLVM のヘッダを 1 つも include することなく。

オペコード、型種別、命令プロパティは LLVM の列挙値ではなく **安定したスキーマ
ID** です。この間接性があるからこそ、今日コンパイルしたプラグインはホストが新しい
LLVM リリースへ移っても動き続けます。

## インターフェイス

```c
#include "neverc/Plugin/PluginIR.h"
```

| インターフェイス | テーブル | スロット | 目的 |
|---|---|--:|---|
| `NEVERC_INTERFACE_IR_CORE_{HIGH,LOW}` | `NevercIRCoreAPI` | 99 | モジュール、値、型、定数、メタデータ、属性の読み書き |
| `NEVERC_INTERFACE_IR_BUILDER_{HIGH,LOW}` | `NevercIRBuilderAPI` | 29 | トランザクショナルな構築 |
| `NEVERC_INTERFACE_IR_ANALYSIS_{HIGH,LOW}` | `NevercIRAnalysisAPI` | 13 | 組み込み解析とプラグイン解析 |
| `NEVERC_INTERFACE_IR_PASS_{HIGH,LOW}` | `NevercIRPassAPI` | 1 | `RegisterPass` |
| `NEVERC_INTERFACE_IR_GEN_{HIGH,LOW}` | `NevercIRGenAPI` | 5 | SemanticUnit → IR の低下処理を置き換える |
| `NEVERC_INTERFACE_IR_OPTIMIZATION_{HIGH,LOW}` | `NevercIROptimizationAPI` | 7 | 最適化パイプライン全体を置き換える |

いずれも major 1 では `NEVERC_INTERFACE_STABLE` です。対応する
`NEVERC_IR_*_API_MAJOR` / `_MINOR` で交渉し、`TableSize` が自分の呼ぶ最後の
スロットまで届いているか、[`pluginsdk/examples/FunctionPass.c`] と同じように検証して
ください:

```c
Status = Bootstrap->QueryInterface(
    Bootstrap->Context,
    (NevercInterfaceID){NEVERC_INTERFACE_IR_PASS_HIGH,
                        NEVERC_INTERFACE_IR_PASS_LOW},
    NEVERC_IR_PASS_API_MAJOR, NEVERC_IR_PASS_API_MINOR, &Table, &Minor,
    &StructSize);
if (!Table ||
    StructSize < offsetof(NevercIRPassAPI, RegisterPass) +
                     sizeof(((NevercIRPassAPI *)0)->RegisterPass))
  return fail(NEVERC_STATUS_ABI_MISMATCH);
```

## フェーズ

IR フェーズは 8 つです:

| フェーズ | ポリシー |
|---|---|
| `neverc.ir.generate` | OBSERVABLE、INTERCEPTABLE、REPLACEABLE |
| `neverc.ir.optimize` | OBSERVABLE、INTERCEPTABLE、REPLACEABLE |
| `neverc.ir.pass.pre_opt` | OBSERVABLE、INTERCEPTABLE |
| `neverc.ir.pass.pipeline_start` | OBSERVABLE、INTERCEPTABLE |
| `neverc.ir.pass.optimizer_last` | OBSERVABLE、INTERCEPTABLE |
| `neverc.ir.pass.post_opt` | OBSERVABLE、INTERCEPTABLE |
| `neverc.ir.pass.pre_codegen` | OBSERVABLE、INTERCEPTABLE |
| `neverc.ir.final_verify` | OBSERVABLE、**封印されたホストゲート** |

5 つの `pass.*` フェーズが `NevercIRPassDescriptor.Phase` の指す先です。
`neverc.ir.final_verify` は LLVM の検証器を走らせ、最適化プロバイダを含め、何者に
も傍受・置換・スキップされません。

## スキーマ

[`Schema/PluginIRSchema.inc`] は生成物で、[`PluginIR.h`] が include します。ダイジェ
ストと以下の定数群を公開します:

```c
#define NEVERC_IR_SCHEMA_CAPABILITY_MAJOR   UINT16_C(1)
#define NEVERC_IR_SCHEMA_DIGEST             "4302919d…"
#define NEVERC_IR_TYPE_KIND_COUNT           UINT32_C(22)
#define NEVERC_IR_VALUE_KIND_COUNT          UINT32_C(29)
#define NEVERC_IR_OPCODE_COUNT              UINT32_C(67)
#define NEVERC_IR_PREDICATE_COUNT           UINT32_C(26)
#define NEVERC_IR_LINKAGE_COUNT             UINT32_C(11)
#define NEVERC_IR_CALLING_CONVENTION_COUNT  UINT32_C(21)
#define NEVERC_IR_PROPERTY_COUNT            UINT32_C(23)
```

ID は上位バイトで領域が付されます —— 型は `0x41……`、値種別は `0x42……`、
オペコードは `0x43……`、プロパティは `0x49……` —— ので、誤った位置で使われた値は
読み違えられるのではなく拒否されます。

## ハンドルと所有権

IR ハンドルは 1 つのタスクにスコープされた不透明な `{Owner, Value}` の組で、その
背後にあるものはすべてホストの所有物です。

- コールバックやタスクが終わった後にハンドルを保持しないこと。
- 別のセッションやタスクでハンドルを使わないこと。
- コミットされた置換は、置き換えられたオブジェクトのハンドルを無効化します。
- 中止された変更は、その変更が作ったハンドルを失効させます。
- エラーは `NEVERC_STATUS_STALE_HANDLE`、`WRONG_SCOPE`、`WRONG_TYPE` であり、
  生の LLVM ポインタが返ることは決してありません。

問い合わせから返る文字列やバイトのビューはコールバックの間だけ借用されます。唯一
の例外は `ExportModule` で、これは `NevercIRSerializedBufferHandle` を返すので、
`ReleaseSerializedBuffer` へ返さなければなりません。

## モジュールの走査

コレクションは自身の世代を持つカーソル経由で読みます。そのため走査の途中で変更が
起きても、黙って項目を飛ばすのではなく検出されます:

```c
NevercIRValueCursor Cursor = {0};
Cursor.Header = (NevercABITableHeader){sizeof(Cursor),
                                       NEVERC_IR_CORE_API_MAJOR,
                                       NEVERC_IR_CORE_API_MINOR, 0};
Core->BeginValueCursor(Core->Context, Task, Module,
                       NEVERC_IR_COLLECTION_MODULE_FUNCTIONS, &Cursor);

NevercIRValueHandle Batch[32];
uint64_t Count = 0;
for (;;) {
  Core->CollectValueCursor(Core->Context, Task, &Cursor, Batch, 32, &Count);
  if (Count == 0)
    break;
  for (uint64_t I = 0; I != Count; ++I) {
    NevercStringView Name;
    Core->GetValueName(Core->Context, Task, Batch[I], &Name);
  }
}
```

`Count` が 0 で返るまで繰り返します。7 つのコレクションは
`MODULE_FUNCTIONS`、`MODULE_GLOBALS`、`MODULE_ALIASES`、`MODULE_I_FUNCS`、
`FUNCTION_ARGUMENTS`、`FUNCTION_BLOCKS`、`BLOCK_INSTRUCTIONS` です。

それ以外はすべて直接の問い合わせです: `GetValueKind`、`GetValueType`、
`GetOperandCount` / `GetOperand` / `SetOperand`、`GetValueUseCount` /
`GetValueUse`、`GetTerminator`、`GetPredecessor*`、`GetSuccessor*`、
`GetPHIIncoming*`、そしてモジュール単位の `GetModuleIdentifier`、
`GetModuleTargetTriple`、`GetModuleDataLayout`、`GetModuleInlineAssembly` と
それらのセッター。

## 型と定数

型はインターン化されているので、二度尋ねても同じハンドルが返ります:

```c
NevercIRTypeHandle I32, Ptr, Fn;
Core->GetIntegerType(Core->Context, Task, 32, &I32);
Core->GetPointerType(Core->Context, Task, /*AddressSpace=*/0, &Ptr);

NevercIRTypeHandle Params[] = {I32, Ptr};
Core->GetFunctionType(Core->Context, Task, I32, Params, 2,
                      /*Variadic=*/0, &Fn);
```

`GetPrimitiveType` は `NEVERC_IR_TYPE_VOID`、`_FLOAT`、`_DOUBLE`、`_TOKEN` の
ようなスキーマ種別を取ります。残りは `GetArrayType`、`GetVectorType`
（`Scalable` フラグ付き）、`GetStructType`（名前付きかリテラルか、packed かどうか）
が担います。

整数と浮動小数点の定数はリトルエンディアンの 64 ビットワードから組み立てるので、
`i128` にも特別な経路は要りません:

```c
uint64_t Words[2] = {0xFFFFFFFFFFFFFFFFULL, 0x1ULL};
NevercIRValueHandle C;
Core->CreateIntegerConstant(Core->Context, Task, I128, Words, 2, &C);
```

`GetNullConstant`、`GetPoisonConstant`、`GetUndefConstant`、
`CreateAggregateConstant`、`GetGlobalAddressConstant` が単純な場合を、
`CreateConstantBinaryExpression`、`CreateConstantCastExpression`、
`CreateConstantCompareExpression`、`CreateConstantGEPExpression` が定数式を
構築します。

## 命令プロパティ

フラグごとにアクセサを置く代わりに、命令の細部はスキーマ ID をキーとするタグ付き
プロパティ値を通ります:

```c
typedef struct NevercIRPropertyValue {
  NevercABITableHeader Header;
  NevercIRPropertyValueKind Kind;   /* BOOL, UINT, ENUM, FLAGS, STRING, TYPE */
  uint32_t Reserved;
  uint64_t UnsignedValue;
  NevercIRTypeHandle TypeValue;
  NevercStringView StringValue;
} NevercIRPropertyValue;

NevercIRPropertyValue Value = {0};
Value.Header = /* … */;
Core->GetInstructionProperty(Core->Context, Task, Instruction,
                             NEVERC_IR_PROPERTY_ALIGNMENT, &Value);
```

23 のプロパティは `NAME`、`FAST_MATH_FLAGS`、`NUW`、`NSW`、`EXACT`、
`DISJOINT`、`VOLATILE`、`ALIGNMENT`、`ATOMIC_ORDERING`、`SYNC_SCOPE`、
`PREDICATE`、`CALLING_CONVENTION`、`TAIL_CALL_KIND`、`INDICES`、`WEAK`、
`SUCCESS_ORDERING`、`FAILURE_ORDERING`、`INBOUNDS`、`SOURCE_ELEMENT_TYPE`、
`ALLOCATED_TYPE`、`ATTRIBUTES`、`CLEANUP`、`NUSW` です。アトミック順序は
`NOT_ATOMIC` から `SEQUENTIALLY_CONSISTENT` まで、tail-call 種別は `NONE`、
`TAIL`、`MUST_TAIL`、`NO_TAIL`、fast-math フラグは `ALLOW_REASSOC` から
`APPROX_FUNC` までのおなじみの 7 ビットです。

## 属性

属性は作ってから付ける値であり、そのおかげで 4 つの種類（`ENUM`、`INTEGER`、
`STRING`、`TYPE`）が一様に扱えます:

```c
NevercIRAttributeHandle NoInline;
Core->CreateEnumAttribute(Core->Context, Task, SV("noinline"), &NoInline);
Core->AddFunctionAttribute(Core->Context, Task, Function,
                           NEVERC_IR_ATTRIBUTE_LOCATION_FUNCTION,
                           /*ParameterIndex=*/0, NoInline);

NevercBool Present = NEVERC_FALSE;
Core->HasFunctionAttribute(Core->Context, Task, Function, SV("noinline"),
                           &Present);
```

[`pluginsdk/examples/CustomCallConvPlugin.c`] はこれを
`GetFunctionStringAttribute` と組み合わせ、データで定義された呼び出し規約を動かし
ます。

## トランザクショナルな変更

構造的な変更は `NevercIRBuilderAPI` を通ります。変更（mutation）がトランザク
ションで、ビルダはその内側のカーソルです。

```c
NevercIRMutationHandle Mutation;
NevercIRBuilderHandle Builder;

Builders->BeginMutation(Builders->Context, Task,
                        NEVERC_IR_MUTATION_SCOPE_FUNCTION, Function,
                        &Mutation);
Builders->CreateBuilder(Builders->Context, Task, Mutation, &Builder);
Builders->SetInsertBefore(Builders->Context, Task, Builder, Terminator);

NevercIRValueHandle Sum;
Builders->BuildBinary(Builders->Context, Task, Builder,
                      NEVERC_IR_OPCODE_ADD, Left, Right, SV("sum"), &Sum);

Status = Builders->CommitMutation(Builders->Context, Task, Mutation);
if (Status.Code != NEVERC_STATUS_OK)
  Builders->AbortMutation(Builders->Context, Task, Mutation);

Builders->DestroyBuilder(Builders->Context, Task, Builder);
Builders->DestroyMutation(Builders->Context, Task, Mutation);
```

スコープは `NEVERC_IR_MUTATION_SCOPE_MODULE`、`_FUNCTION`、`_LOOP` で、
`ScopeRoot` が対象の関数やループヘッダを指名します。コミットは候補を検証して
アトミックに公開します —— 検証器が失敗すればホストはロールバックし、以前の
モジュールは手つかずで残ります。

構築呼び出しは `BuildBinary`、`BuildUnary`、`BuildCompare`、`BuildCast`、
`BuildSelect`、`BuildAlloca`、`BuildLoad`、`BuildStore`、`BuildGetElementPtr`、
`BuildCall`、`BuildPhi`、`BuildBranch`、`BuildConditionalBranch`、
`BuildUnreachable`、`BuildReturn`、`BuildReturnVoid` です。`SetDebugLocation` と
`SetFastMathFlags` は、それ以降ビルダが発行するすべてに適用されます。

非対称性に注意してください。`AddPhiIncoming`、`CreateFunction`、
`CreateBasicBlock` はビルダではなく **mutation** を取ります。挿入位置に縛られない
からです。

`DestroyMutation` はコミットや中止とは別物です。すべての `BeginMutation` に
ちょうど 1 つの `DestroyMutation` が要ります。トランザクションがどう終わったかに
関わらず、です。

## パス

```c
NevercIRPassDescriptor Pass = {0};
Pass.Header = (NevercABITableHeader){sizeof(Pass), NEVERC_IR_PASS_API_MAJOR,
                                     NEVERC_IR_PASS_API_MINOR, 0};
Pass.PassID        = SV("example.function-pass");
Pass.Phase         = (NevercInterfaceID){
                         NEVERC_PHASE_IR_PASS_PIPELINE_START_HIGH,
                         NEVERC_PHASE_IR_PASS_PIPELINE_START_LOW};
Pass.Level         = NEVERC_IR_PASS_LEVEL_FUNCTION;
Pass.Deterministic = NEVERC_TRUE;
Pass.Cacheable     = NEVERC_TRUE;
Pass.Run           = run_function;
PassAPI->RegisterPass(PassAPI->Context, RegistrarContext, &Pass);
```

レベルは `MODULE`、`CGSCC`、`FUNCTION`、`LOOP` です。呼び出しにはそのレベルで有効
なハンドルだけが載ります:

```c
typedef struct NevercIRPassInvocation {
  NevercABITableHeader Header;
  NevercTaskHandle Task;
  NevercInterfaceID Phase;
  NevercStringView PassID;
  NevercIRPassLevel Level;
  NevercIROptimizationLevel OptimizationLevel;  /* O0…O3, Os, Oz */
  NevercIRModuleHandle Module;
  NevercIRValueHandle Function;                 /* FUNCTION と LOOP      */
  NevercIRValueHandle LoopHeader;               /* LOOP のみ             */
  const NevercIRValueHandle *SCCFunctions;      /* CGSCC のみ            */
  uint64_t SCCFunctionCount;
  const NevercIRCoreAPI *Core;
  const NevercIRBuilderAPI *Builder;
  const NevercIRAnalysisAPI *Analyses;
  uint64_t Reserved[2];
} NevercIRPassInvocation;
```

3 つの API ポインタは呼び出しと一緒に来るので、パス本体はテーブルを持ち歩く必要が
ありません。

何が保たれたかは `OutPreserved` で報告します:

```c
OutPreserved->Flags = NEVERC_IR_PRESERVE_ALL;   /* または _NONE、_CFG */
```

`NEVERC_IR_PRESERVE_CFG` は、命令が変わっても制御フローグラフは無傷だという意味
です。独自解析は `CustomAnalyses` に列挙することで保存されます。IR を変更した後に
`PRESERVE_ALL` を主張してはいけません —— アダプタはモジュール世代を比較し、偽の
主張を拒否します。

関数パスとループパスは並行して走りうるので、可変のプラグイン状態はそのプラグインが
宣言した `NevercConcurrencyModel` に従わなければなりません。

## 解析

7 つの組み込み解析が ID で問い合わせ可能です: `DOMINATOR_TREE`、
`POST_DOMINATOR_TREE`、`LOOP_INFO`、`SCALAR_EVOLUTION`、`MEMORY_SSA`、
`CALL_GRAPH`、`ALIAS`。

```c
NevercIRAnalysisResultHandle Loops;
Analyses->QueryBuiltin(Analyses->Context, Task,
                       NEVERC_IR_ANALYSIS_LOOP_INFO, Function, &Loops);

uint64_t LoopCount = 0;
Analyses->GetLoopCount(Analyses->Context, Task, Loops, &LoopCount);
for (uint64_t I = 0; I != LoopCount; ++I) {
  NevercIRValueHandle Header;
  Analyses->GetLoopHeader(Analyses->Context, Task, Loops, I, &Header);
}
```

いずれも不透明な塊ではなく型付きのアクセサを持ちます:
`DominatorTreeDominates`、`GetLoopCount` / `GetLoopHeader` /
`GetLoopForBlock`、`GetScalarEvolutionConstantTripCount`、
`GetMemoryAccessKind`（`NONE`、`USE`、`DEF`、`PHI`、`LIVE_ON_ENTRY`）、
`GetDirectCalleeCount` / `GetDirectCallee`、そして `Alias`（`NO`、`MAY`、
`PARTIAL`、`MUST`）。

プラグイン解析は自前のライフサイクルとともに登録します:

```c
NevercIRAnalysisDescriptor Analysis = {0};
Analysis.Header          = /* … */;
Analysis.AnalysisID      = MyAnalysisID;
Analysis.Name            = SV("example.my-analysis");
Analysis.Level           = NEVERC_IR_PASS_LEVEL_FUNCTION;
Analysis.Dependencies    = Deps;
Analysis.DependencyCount = DepCount;
Analysis.Compute         = compute;
Analysis.Query           = query;
Analysis.Invalidate      = invalidate;
Analysis.Destroy         = destroy;
Analyses->RegisterAnalysis(Analyses->Context, RegistrarContext, &Analysis);
```

`Invalidate` には理由が伝えられます —— `INVALIDATED_BY_PASS` か
`INVALIDATED_BY_PLAN_DESTROY`。結果は呼び出しごとにキャッシュされ、走っている
パスが何を保存したかに応じて捨てられます。依存の循環は登録時に拒否され、解析
コールバックの中から IR を変更することも拒まれます。

## 生成と最適化の置き換え

`NevercIRGenAPI` は `neverc.ir.generate` を置き換えます:

```c
NevercIRGeneratePhaseInput In = {0};
In.Header = /* … */;
Gen->GetGeneratePhaseInput(Gen->Context, Frame, Frame->Input, &In);
/* In.SemanticUnit、.TargetTriple、.DataLayout、.SourceIdentity、
   .SourceDigest */

const NevercIRCoreAPI *Core;
const NevercIRBuilderAPI *Builders;
Gen->CreateModule(Gen->Context, Frame, SV("my.module"), &Core, &Builders);
/* … モジュールを構築する … */

NevercIRModuleArtifactDescriptor Descriptor = {0};
Descriptor.Header           = /* … */;
Descriptor.Product          = MyProductID;
Descriptor.DependencyDigest = Digest;
Gen->PublishModule(Gen->Context, Frame, &Descriptor, &Output);
```

`ImportModule` は空のモジュールではなく、ビットコードやテキスト IR から始めます。
`NevercIROptimizationAPI` は `neverc.ir.optimize` に対して同じ形をしており、
加えて入力モジュールへ届く `GetInputModule` と、組み込みパイプラインへ委譲して
その結果を後処理する `RunBuiltinPipeline` を備えます。

どちらの経路もポインタを返すのではなくホストを通じて公開し、ターゲット互換性を
検証し、公開に失敗すれば古いモジュールをアトミックに保ちます。その後も
`neverc.ir.final_verify` は必ず走ります。

## 例

| ファイル | 示すもの |
|---|---|
| [`pluginsdk/examples/FunctionPass.c`] | ABI 交渉込みの、読み取り専用の関数パス |
| [`pluginsdk/examples/ExamplePlugin.c`] | 値カーソルで関数を走査するモジュールレベルのパス |
| [`pluginsdk/examples/CustomCallConvPlugin.c`] | 属性と呼び出し位置のプロパティ |

```sh
cmake --build build-neverc --target neverc-plugin-example-function-pass
build-neverc/bin/neverc \
  -fplugin=build-neverc/neverc/pluginsdk/examples/host/FunctionPass.so \
  -O2 -c input.c -o input.o
```

CMake がお使いのプラットフォーム向けに生成したモジュール拡張子を使ってください。

## 規則

- すべてのコールバックから `NevercStatus` を返してください。プラグインの失敗は
  構造化された診断になります。例外を C の境界越しに出してはいけません。
- 値を埋める呼び出しの前に、出力構造体をゼロで初期化し `Header` を設定してくだ
  さい。
- オペコード、型、プロパティの数値を直書きしないでください。[`PluginIRSchema.inc`]
  の名前を使えば、スキーマ改訂がコンパイルエラーになります。
- すべての `BeginMutation` にちょうど 1 つの `DestroyMutation` を、すべての
  `CreateBuilder` にちょうど 1 つの `DestroyBuilder` を、エラー経路も含めて対応
  させてください。
- `ExportModule` が渡すものは `ReleaseSerializedBuffer` で解放してください。
- IR を変更した後に `NEVERC_IR_PRESERVE_ALL` を主張しないでください。
- プラグインが `NEVERC_CONCURRENCY_SESSION_SERIAL` を宣言していない限り、関数
  パスとループパスは並列に走ると想定してください。
- `neverc.ir.final_verify` は封印されています。プラグインが何をしてもこれを飛ばす
  ことはできません。

規範的な宣言、スキーマそのもの、生成される定数、フェーズポリシー、テストの証拠は、
[`PluginIR.h`]、[`Schema/IRSchema.json`]、[`Schema/PluginIRSchema.inc`]、
[`Schema/PhaseSchema.json`]、[`coverage.json`] を参照してください。

<!-- reference links -->
[`coverage.json`]: coverage.json
[`PluginIR.h`]: ../../neverc/include/neverc/Plugin/PluginIR.h
[`PluginIRSchema.inc`]: ../../neverc/include/neverc/Plugin/Schema/PluginIRSchema.inc
[`pluginsdk/examples/CustomCallConvPlugin.c`]: ../../pluginsdk/examples/CustomCallConvPlugin.c
[`pluginsdk/examples/ExamplePlugin.c`]: ../../pluginsdk/examples/ExamplePlugin.c
[`pluginsdk/examples/FunctionPass.c`]: ../../pluginsdk/examples/FunctionPass.c
[`Schema/IRSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/IRSchema.json
[`Schema/PhaseSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PhaseSchema.json
[`Schema/PluginIRSchema.inc`]: ../../neverc/include/neverc/Plugin/Schema/PluginIRSchema.inc
