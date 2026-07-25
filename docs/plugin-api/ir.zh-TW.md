**語言**: [English](ir.md) | [简体中文](ir.zh-CN.md) | [繁體中文](ir.zh-TW.md) | [日本語](ir.ja.md) | [한국어](ir.ko.md) | [Français](ir.fr.md) | [Deutsch](ir.de.md) | [Español](ir.es.md) | [Italiano](ir.it.md) | [Русский](ir.ru.md) | [العربية](ir.ar.md)

# NeverC 外掛 IR API

`PluginIR.h` 透過六張能力表與一份產生的 schema 公開 LLVM IR。外掛可以讀寫 IR、
在五個穩定的流水線點註冊 pass、定義自己的分析，或是乾脆整個取代 IR 產生與最佳化
流水線──而且完全不需要包含任何一個 LLVM 標頭檔。

opcode、型別種類與指令屬性都是**穩定的 schema ID**，而不是 LLVM 的列舉值。正是
這層間接，讓今天編出來的外掛在主機換到新的 LLVM 版本之後仍能繼續運作。

## 介面

```c
#include "neverc/Plugin/PluginIR.h"
```

| 介面 | 表 | 槽位 | 用途 |
|---|---|--:|---|
| `NEVERC_INTERFACE_IR_CORE_{HIGH,LOW}` | `NevercIRCoreAPI` | 99 | 讀寫模組、值、型別、常數、metadata、屬性 |
| `NEVERC_INTERFACE_IR_BUILDER_{HIGH,LOW}` | `NevercIRBuilderAPI` | 29 | 交易式建構 |
| `NEVERC_INTERFACE_IR_ANALYSIS_{HIGH,LOW}` | `NevercIRAnalysisAPI` | 13 | 內建與外掛分析 |
| `NEVERC_INTERFACE_IR_PASS_{HIGH,LOW}` | `NevercIRPassAPI` | 1 | `RegisterPass` |
| `NEVERC_INTERFACE_IR_GEN_{HIGH,LOW}` | `NevercIRGenAPI` | 5 | 取代 SemanticUnit → IR 的降階 |
| `NEVERC_INTERFACE_IR_OPTIMIZATION_{HIGH,LOW}` | `NevercIROptimizationAPI` | 7 | 取代整個最佳化流水線 |

每一張在 major 1 都是 `NEVERC_INTERFACE_STABLE`。請用對應的
`NEVERC_IR_*_API_MAJOR` / `_MINOR` 協商，並驗證 `TableSize` 至少涵蓋你會呼叫的
最後一個槽位，就像 `pluginsdk/examples/FunctionPass.c` 那樣：

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

## 階段

八個 IR 階段：

| 階段 | 政策 |
|---|---|
| `neverc.ir.generate` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE |
| `neverc.ir.optimize` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE |
| `neverc.ir.pass.pre_opt` | OBSERVABLE, INTERCEPTABLE |
| `neverc.ir.pass.pipeline_start` | OBSERVABLE, INTERCEPTABLE |
| `neverc.ir.pass.optimizer_last` | OBSERVABLE, INTERCEPTABLE |
| `neverc.ir.pass.post_opt` | OBSERVABLE, INTERCEPTABLE |
| `neverc.ir.pass.pre_codegen` | OBSERVABLE, INTERCEPTABLE |
| `neverc.ir.final_verify` | OBSERVABLE、**封印的主機閘門** |

那五個 `pass.*` 階段就是 `NevercIRPassDescriptor.Phase` 指向的地方。
`neverc.ir.final_verify` 會執行 LLVM 驗證器，任何東西都不能攔截、取代或跳過它
──包含最佳化 Provider 在內。

## Schema

`Schema/PluginIRSchema.inc` 是產生出來的，並由 `PluginIR.h` 包含。它發布一份摘要
與這些常數集合：

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

ID 用高位位元組標記所屬領域──`0x41……` 是型別、`0x42……` 是值種類、`0x43……`
是 opcode、`0x49……` 是屬性──所以放錯位置的值會被拒絕，而不是被誤讀。

## 控制代碼與所有權

IR 控制代碼是範圍限於單一任務的不透明 `{Owner, Value}` 對，它們背後的東西全都由
主機擁有。

- 絕不要在回呼或任務結束後還留著控制代碼。
- 絕不要在另一個 session 或任務裡使用某個控制代碼。
- 已提交的替換會讓被替換物件的控制代碼失效。
- 被中止的變更會讓該變更所建立的控制代碼變成過期。
- 錯誤是 `NEVERC_STATUS_STALE_HANDLE`、`WRONG_OWNER` 或 `WRONG_TYPE`──絕不會是
  一個裸的 LLVM 指標。

查詢回傳的字串與位元組 view 只在該回呼期間借用。唯一的例外是 `ExportModule`，
它回傳一個 `NevercIRSerializedBufferHandle`，你必須把它交還給
`ReleaseSerializedBuffer`。

## 走訪模組

集合是透過帶有自身世代編號的游標讀取的，所以走到一半發生變更時會被偵測到，而不
是默默跳過條目：

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

一直重複到 `Count` 回傳零為止。七種集合分別是 `MODULE_FUNCTIONS`、
`MODULE_GLOBALS`、`MODULE_ALIASES`、`MODULE_I_FUNCS`、`FUNCTION_ARGUMENTS`、
`FUNCTION_BLOCKS` 與 `BLOCK_INSTRUCTIONS`。

其餘一切都是直接查詢：`GetValueKind`、`GetValueType`、`GetOperandCount` /
`GetOperand` / `SetOperand`、`GetValueUseCount` / `GetValueUse`、
`GetTerminator`、`GetPredecessor*`、`GetSuccessor*`、`GetPHIIncoming*`，以及模組
層級的 `GetModuleIdentifier`、`GetModuleTargetTriple`、`GetModuleDataLayout`、
`GetModuleInlineAssembly` 和它們對應的 setter。

## 型別與常數

型別是內部化（interned）的，所以問兩次會得到同一個控制代碼：

```c
NevercIRTypeHandle I32, Ptr, Fn;
Core->GetIntegerType(Core->Context, Task, 32, &I32);
Core->GetPointerType(Core->Context, Task, /*AddressSpace=*/0, &Ptr);

NevercIRTypeHandle Params[] = {I32, Ptr};
Core->GetFunctionType(Core->Context, Task, I32, Params, 2,
                      /*Variadic=*/0, &Fn);
```

`GetPrimitiveType` 接受 schema 種類，例如 `NEVERC_IR_TYPE_VOID`、`_FLOAT`、
`_DOUBLE` 或 `_TOKEN`；其餘則由 `GetArrayType`、`GetVectorType`（帶一個
`Scalable` 旗標）與 `GetStructType`（具名或字面、packed 或否）涵蓋。

整數與浮點常數是用小端序的 64 位元字組建構的，所以 `i128` 不需要任何特別路徑：

```c
uint64_t Words[2] = {0xFFFFFFFFFFFFFFFFULL, 0x1ULL};
NevercIRValueHandle C;
Core->CreateIntegerConstant(Core->Context, Task, I128, Words, 2, &C);
```

`GetNullConstant`、`GetPoisonConstant`、`GetUndefConstant`、
`CreateAggregateConstant` 與 `GetGlobalAddressConstant` 涵蓋簡單情況；
`CreateConstantBinaryExpression`、`CreateConstantCastExpression`、
`CreateConstantCompareExpression` 與 `CreateConstantGEPExpression` 則建構常數運算
式。

## 指令屬性

指令的細節不是靠每個旗標一個存取器，而是走一個以 schema ID 為鍵的帶標籤屬性值：

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

這 23 個屬性是 `NAME`、`FAST_MATH_FLAGS`、`NUW`、`NSW`、`EXACT`、`DISJOINT`、
`VOLATILE`、`ALIGNMENT`、`ATOMIC_ORDERING`、`SYNC_SCOPE`、`PREDICATE`、
`CALLING_CONVENTION`、`TAIL_CALL_KIND`、`INDICES`、`WEAK`、`SUCCESS_ORDERING`、
`FAILURE_ORDERING`、`INBOUNDS`、`SOURCE_ELEMENT_TYPE`、`ALLOCATED_TYPE`、
`ATTRIBUTES`、`CLEANUP` 與 `NUSW`。原子序從 `NOT_ATOMIC` 一路到
`SEQUENTIALLY_CONSISTENT`；tail-call 種類有 `NONE`、`TAIL`、`MUST_TAIL` 與
`NO_TAIL`；fast-math 旗標則是從 `ALLOW_REASSOC` 到 `APPROX_FUNC` 那慣常的七個位
元。

## 屬性

屬性是先建立、再附加的值，這讓四種型態（`ENUM`、`INTEGER`、`STRING`、`TYPE`）保
持一致：

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

`pluginsdk/examples/CustomCallConvPlugin.c` 就是把這個和
`GetFunctionStringAttribute` 搭配起來，驅動一個由資料定義的呼叫慣例。

## 交易式變更

結構性的改動都要走 `NevercIRBuilderAPI`。變更（mutation）是那筆交易，建構器則是
交易內部的游標。

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

範圍有 `NEVERC_IR_MUTATION_SCOPE_MODULE`、`_FUNCTION` 與 `_LOOP`；`ScopeRoot`
指名那個函式或迴圈標頭。提交會驗證候選內容並原子性地發布──驗證器失敗時，主機會
回捲，先前的模組毫髮無傷。

建構呼叫有 `BuildBinary`、`BuildUnary`、`BuildCompare`、`BuildCast`、
`BuildSelect`、`BuildAlloca`、`BuildLoad`、`BuildStore`、`BuildGetElementPtr`、
`BuildCall`、`BuildPhi`、`BuildBranch`、`BuildConditionalBranch`、
`BuildUnreachable`、`BuildReturn` 與 `BuildReturnVoid`。`SetDebugLocation` 與
`SetFastMathFlags` 會套用到建構器之後發出的所有東西。

注意這個不對稱：`AddPhiIncoming`、`CreateFunction` 與 `CreateBasicBlock` 吃的是
**mutation**，而不是建構器，因為它們並不繫結到插入位置。

`DestroyMutation` 和 commit、abort 是分開的。每個 `BeginMutation` 都恰好需要一個
`DestroyMutation`，無論那筆交易最後是怎麼結束的。

## Pass

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

層級有 `MODULE`、`CGSCC`、`FUNCTION` 與 `LOOP`。每次呼叫只會帶著在該層級有效的
控制代碼：

```c
typedef struct NevercIRPassInvocation {
  NevercABITableHeader Header;
  NevercTaskHandle Task;
  NevercInterfaceID Phase;
  NevercStringView PassID;
  NevercIRPassLevel Level;
  NevercIROptimizationLevel OptimizationLevel;  /* O0…O3, Os, Oz */
  NevercIRModuleHandle Module;
  NevercIRValueHandle Function;                 /* FUNCTION 與 LOOP       */
  NevercIRValueHandle LoopHeader;               /* 僅 LOOP                */
  const NevercIRValueHandle *SCCFunctions;      /* 僅 CGSCC               */
  uint64_t SCCFunctionCount;
  const NevercIRCoreAPI *Core;
  const NevercIRBuilderAPI *Builder;
  const NevercIRAnalysisAPI *Analyses;
  uint64_t Reserved[2];
} NevercIRPassInvocation;
```

那三個 API 指標會跟著呼叫一起來，所以 pass 本體不需要自己存一份表。

透過 `OutPreserved` 回報什麼東西保住了：

```c
OutPreserved->Flags = NEVERC_IR_PRESERVE_ALL;   /* 或 _NONE、或 _CFG */
```

`NEVERC_IR_PRESERVE_CFG` 表示即使指令改了，控制流程圖仍然完好。自訂分析要列在
`CustomAnalyses` 裡才會被保留。改了 IR 之後不要宣稱 `PRESERVE_ALL`──轉接層會
比對模組世代，並拒絕不實的宣稱。

函式 pass 與迴圈 pass 可能並行執行，所以可變的外掛狀態必須符合該外掛宣告的
`NevercConcurrencyModel`。

## 分析

七種內建分析可以用 ID 查詢：`DOMINATOR_TREE`、`POST_DOMINATOR_TREE`、
`LOOP_INFO`、`SCALAR_EVOLUTION`、`MEMORY_SSA`、`CALL_GRAPH` 與 `ALIAS`。

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

每一種都有具型別的存取器，而不是一團不透明的資料：`DominatorTreeDominates`、
`GetLoopCount` / `GetLoopHeader` / `GetLoopForBlock`、
`GetScalarEvolutionConstantTripCount`、`GetMemoryAccessKind`（`NONE`、`USE`、
`DEF`、`PHI`、`LIVE_ON_ENTRY`）、`GetDirectCalleeCount` / `GetDirectCallee`，以及
`Alias`（`NO`、`MAY`、`PARTIAL`、`MUST`）。

外掛分析註冊時帶著自己的生命週期：

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

`Invalidate` 會被告知原因──`INVALIDATED_BY_PASS` 或
`INVALIDATED_BY_PLAN_DESTROY`。結果會依每次呼叫做快取，並依照執行中的 pass 保留
了什麼而被丟棄。相依環路在註冊時就會被拒絕，而從分析回呼裡變更 IR 也會被拒絕。

## 取代產生與最佳化

`NevercIRGenAPI` 取代 `neverc.ir.generate`：

```c
NevercIRGeneratePhaseInput In = {0};
In.Header = /* … */;
Gen->GetGeneratePhaseInput(Gen->Context, Frame, Frame->Input, &In);
/* In.SemanticUnit、.TargetTriple、.DataLayout、.SourceIdentity、
   .SourceDigest */

const NevercIRCoreAPI *Core;
const NevercIRBuilderAPI *Builders;
Gen->CreateModule(Gen->Context, Frame, SV("my.module"), &Core, &Builders);
/* … 建構模組 … */

NevercIRModuleArtifactDescriptor Descriptor = {0};
Descriptor.Header           = /* … */;
Descriptor.Product          = MyProductID;
Descriptor.DependencyDigest = Digest;
Gen->PublishModule(Gen->Context, Frame, &Descriptor, &Output);
```

`ImportModule` 讓你從 bitcode 或文字 IR 起步，而不是從空模組開始。
`NevercIROptimizationAPI` 對 `neverc.ir.optimize` 有相同的形狀，另外加上
`GetInputModule` 以取得進來的模組，以及 `RunBuiltinPipeline` 以委派給內建流水線
再對其結果做後處理。

這兩條路都是透過主機發布而不是回傳指標，都會驗證目標相容性，而且在發布失敗時都會
原子性地保住舊模組。之後 `neverc.ir.final_verify` 照樣會執行。

## 範例

| 檔案 | 展示 |
|---|---|
| `pluginsdk/examples/FunctionPass.c` | 一個唯讀的函式 pass，含 ABI 協商 |
| `pluginsdk/examples/ExamplePlugin.c` | 用值游標走訪函式的模組層級 pass |
| `pluginsdk/examples/CustomCallConvPlugin.c` | 屬性與呼叫點屬性 |

```sh
cmake --build build-neverc --target neverc-plugin-example-function-pass
build-neverc/bin/neverc \
  -fplugin=build-neverc/neverc/pluginsdk/examples/host/FunctionPass.so \
  -O2 -c input.c -o input.o
```

請使用 CMake 為你的平台產生的模組副檔名。

## 規則

- 每個回呼都要回傳 `NevercStatus`。外掛的失敗會變成結構化診斷；絕不要讓例外跨越
  C 邊界。
- 在會被填值的呼叫之前，把每個輸出結構清零並設好它的 `Header`。
- 不要寫死 opcode、型別或屬性的數值。請用 `PluginIRSchema.inc` 裡的名稱，這樣
  schema 一改版就會變成編譯錯誤。
- 每個 `BeginMutation` 都恰好對到一個 `DestroyMutation`，每個 `CreateBuilder` 恰
  好對到一個 `DestroyBuilder`，錯誤路徑上也一樣。
- `ExportModule` 交給你的東西要用 `ReleaseSerializedBuffer` 釋放。
- 改過 IR 之後絕不要宣稱 `NEVERC_IR_PRESERVE_ALL`。
- 除非外掛宣告了 `NEVERC_CONCURRENCY_SESSION_SERIAL`，否則就假設函式 pass 與迴圈
  pass 會平行執行。
- `neverc.ir.final_verify` 是封印的。外掛做什麼都跳不過它。

規範性宣告、schema 常數、階段政策與測試證據，請見 `PluginIR.h`、
`Schema/PluginIRSchema.inc`、`Schema/PhaseSchema.json` 與 `coverage.json`。
