**语言**: [English](ir.md) | [简体中文](ir.zh-CN.md) | [繁體中文](ir.zh-TW.md) | [日本語](ir.ja.md) | [한국어](ir.ko.md) | [Français](ir.fr.md) | [Deutsch](ir.de.md) | [Español](ir.es.md) | [Italiano](ir.it.md) | [Русский](ir.ru.md) | [العربية](ir.ar.md)

[← NeverC 插件 ABI](README.zh-CN.md)

# NeverC 插件 IR API

[`PluginIR.h`] 通过六张能力表和一份生成的 schema 暴露 LLVM IR。插件可以读写
IR、在五个稳定的流水线位置注册 pass、定义自己的分析，或者干脆替换掉 IR 生成和
整条优化流水线——全程不包含任何一个 LLVM 头文件。

opcode、类型种类和指令属性都是**稳定的 schema ID**，不是 LLVM 的枚举值。正是这
层间接，让今天编译出来的插件在宿主升级到新版 LLVM 之后依然能用。

## 接口

```c
#include "neverc/Plugin/PluginIR.h"
```

| 接口 | 表 | 槽位 | 用途 |
|---|---|--:|---|
| `NEVERC_INTERFACE_IR_CORE_{HIGH,LOW}` | `NevercIRCoreAPI` | 99 | 读写模块、值、类型、常量、元数据、属性 |
| `NEVERC_INTERFACE_IR_BUILDER_{HIGH,LOW}` | `NevercIRBuilderAPI` | 29 | 事务式构造 |
| `NEVERC_INTERFACE_IR_ANALYSIS_{HIGH,LOW}` | `NevercIRAnalysisAPI` | 13 | 内建分析与插件分析 |
| `NEVERC_INTERFACE_IR_PASS_{HIGH,LOW}` | `NevercIRPassAPI` | 1 | `RegisterPass` |
| `NEVERC_INTERFACE_IR_GEN_{HIGH,LOW}` | `NevercIRGenAPI` | 5 | 替换 SemanticUnit → IR 的降级 |
| `NEVERC_INTERFACE_IR_OPTIMIZATION_{HIGH,LOW}` | `NevercIROptimizationAPI` | 7 | 替换整条优化流水线 |

六个都是 major 1 的 `NEVERC_INTERFACE_STABLE`。用对应的
`NEVERC_IR_*_API_MAJOR` / `_MINOR` 协商，并确认 `TableSize` 覆盖到你要调用的最
后一个槽位，[`pluginsdk/examples/FunctionPass.c`] 里就是这么做的：

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

## 阶段

八个 IR 阶段：

| 阶段 | 策略 |
|---|---|
| `neverc.ir.generate` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE |
| `neverc.ir.optimize` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE |
| `neverc.ir.pass.pre_opt` | OBSERVABLE, INTERCEPTABLE |
| `neverc.ir.pass.pipeline_start` | OBSERVABLE, INTERCEPTABLE |
| `neverc.ir.pass.optimizer_last` | OBSERVABLE, INTERCEPTABLE |
| `neverc.ir.pass.post_opt` | OBSERVABLE, INTERCEPTABLE |
| `neverc.ir.pass.pre_codegen` | OBSERVABLE, INTERCEPTABLE |
| `neverc.ir.final_verify` | OBSERVABLE, **SEALED HOST GATE** |

五个 `pass.*` 阶段就是 `NevercIRPassDescriptor.Phase` 指向的位置。
`neverc.ir.final_verify` 运行 LLVM 验证器，任何东西都不能拦截、替换或跳过它
——包括优化 Provider。

## Schema

[`Schema/PluginIRSchema.inc`] 由工具生成，并由 [`PluginIR.h`] 包含。它发布一个摘要
和各类常量集合：

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

ID 的高位字节标记了它所属的域——`0x41……` 是类型、`0x42……` 是值种类、
`0x43……` 是 opcode、`0x49……` 是属性——所以放错位置的值会被拒绝，而不是被误
读。

## 句柄与所有权

IR 句柄是限定在单个任务内的不透明 `{Owner, Value}` 对，背后的一切都归宿主所
有。

- 回调或任务结束后不要再持有句柄。
- 不要跨会话或跨任务使用句柄。
- 一次已提交的替换会让被替换对象的句柄失效。
- 一次被中止的 mutation 会让它创建的句柄变成陈旧句柄。
- 错误以 `NEVERC_STATUS_STALE_HANDLE`、`WRONG_SCOPE` 或 `WRONG_TYPE` 报出——
  永远不会给你一个裸的 LLVM 指针。

查询返回的字符串和字节视图只在回调期间有效。唯一的例外是 `ExportModule`，它返
回一个 `NevercIRSerializedBufferHandle`，你必须把它交还给
`ReleaseSerializedBuffer`。

## 遍历模块

集合通过带自身代号（generation）的游标读取，这样遍历途中发生的修改会被检测到，
而不是悄悄漏掉条目：

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

一直重复到 `Count` 返回零为止。七种集合是 `MODULE_FUNCTIONS`、
`MODULE_GLOBALS`、`MODULE_ALIASES`、`MODULE_I_FUNCS`、`FUNCTION_ARGUMENTS`、
`FUNCTION_BLOCKS`、`BLOCK_INSTRUCTIONS`。

其余都是直接查询：`GetValueKind`、`GetValueType`、`GetOperandCount` /
`GetOperand` / `SetOperand`、`GetValueUseCount` / `GetValueUse`、
`GetTerminator`、`GetPredecessor*`、`GetSuccessor*`、`GetPHIIncoming*`，以及模
块级的 `GetModuleIdentifier`、`GetModuleTargetTriple`、`GetModuleDataLayout`、
`GetModuleInlineAssembly` 及其 setter。

## 类型与常量

类型是内部化（interned）的，问两次会拿到同一个句柄：

```c
NevercIRTypeHandle I32, Ptr, Fn;
Core->GetIntegerType(Core->Context, Task, 32, &I32);
Core->GetPointerType(Core->Context, Task, /*AddressSpace=*/0, &Ptr);

NevercIRTypeHandle Params[] = {I32, Ptr};
Core->GetFunctionType(Core->Context, Task, I32, Params, 2,
                      /*Variadic=*/0, &Fn);
```

`GetPrimitiveType` 接受 schema 种类，例如 `NEVERC_IR_TYPE_VOID`、`_FLOAT`、
`_DOUBLE`、`_TOKEN`；剩下的由 `GetArrayType`、`GetVectorType`（带 `Scalable`
标志）和 `GetStructType`（具名或字面、紧凑或非紧凑）覆盖。

整数和浮点常量由小端 64 位字构造，所以 `i128` 不需要特殊路径：

```c
uint64_t Words[2] = {0xFFFFFFFFFFFFFFFFULL, 0x1ULL};
NevercIRValueHandle C;
Core->CreateIntegerConstant(Core->Context, Task, I128, Words, 2, &C);
```

`GetNullConstant`、`GetPoisonConstant`、`GetUndefConstant`、
`CreateAggregateConstant` 和 `GetGlobalAddressConstant` 覆盖简单情形；
`CreateConstantBinaryExpression`、`CreateConstantCastExpression`、
`CreateConstantCompareExpression`、`CreateConstantGEPExpression` 构造常量表达
式。

## 指令属性

指令细节不是每个标志一个访问器，而是走一个按 schema ID 索引的带标签值：

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

23 个属性是 `NAME`、`FAST_MATH_FLAGS`、`NUW`、`NSW`、`EXACT`、`DISJOINT`、
`VOLATILE`、`ALIGNMENT`、`ATOMIC_ORDERING`、`SYNC_SCOPE`、`PREDICATE`、
`CALLING_CONVENTION`、`TAIL_CALL_KIND`、`INDICES`、`WEAK`、`SUCCESS_ORDERING`、
`FAILURE_ORDERING`、`INBOUNDS`、`SOURCE_ELEMENT_TYPE`、`ALLOCATED_TYPE`、
`ATTRIBUTES`、`CLEANUP`、`NUSW`。原子序从 `NOT_ATOMIC` 到
`SEQUENTIALLY_CONSISTENT`；尾调用种类是 `NONE`、`TAIL`、`MUST_TAIL`、
`NO_TAIL`；fast-math 标志是从 `ALLOW_REASSOC` 到 `APPROX_FUNC` 的常见七位。

## 属性（Attribute）

属性是先创建再附加的值，这样四种形态（`ENUM`、`INTEGER`、`STRING`、`TYPE`）保
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

[`pluginsdk/examples/CustomCallConvPlugin.c`] 把它与
`GetFunctionStringAttribute` 结合，驱动一个数据定义的调用约定。

## 事务式修改

结构性变更走 `NevercIRBuilderAPI`。mutation 是事务，builder 是事务内的游标。

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

作用域有 `NEVERC_IR_MUTATION_SCOPE_MODULE`、`_FUNCTION`、`_LOOP`；`ScopeRoot`
指明函数或循环头。提交会校验候选结果并原子发布——验证器失败时宿主回滚，之前
的模块原封不动。

构造调用有 `BuildBinary`、`BuildUnary`、`BuildCompare`、`BuildCast`、
`BuildSelect`、`BuildAlloca`、`BuildLoad`、`BuildStore`、`BuildGetElementPtr`、
`BuildCall`、`BuildPhi`、`BuildBranch`、`BuildConditionalBranch`、
`BuildUnreachable`、`BuildReturn`、`BuildReturnVoid`。`SetDebugLocation` 和
`SetFastMathFlags` 作用于此后 builder 发出的所有指令。

注意这里的不对称：`AddPhiIncoming`、`CreateFunction`、`CreateBasicBlock` 接收的
是 **mutation** 而不是 builder，因为它们与插入点无关。

`DestroyMutation` 独立于 commit 和 abort。无论事务以哪种方式结束，每个
`BeginMutation` 都需要恰好一次 `DestroyMutation`。

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

层级有 `MODULE`、`CGSCC`、`FUNCTION`、`LOOP`。调用信息只携带该层级下有效的句
柄：

```c
typedef struct NevercIRPassInvocation {
  NevercABITableHeader Header;
  NevercTaskHandle Task;
  NevercInterfaceID Phase;
  NevercStringView PassID;
  NevercIRPassLevel Level;
  NevercIROptimizationLevel OptimizationLevel;  /* O0…O3, Os, Oz */
  NevercIRModuleHandle Module;
  NevercIRValueHandle Function;                 /* FUNCTION 和 LOOP      */
  NevercIRValueHandle LoopHeader;               /* 仅 LOOP               */
  const NevercIRValueHandle *SCCFunctions;      /* 仅 CGSCC              */
  uint64_t SCCFunctionCount;
  const NevercIRCoreAPI *Core;
  const NevercIRBuilderAPI *Builder;
  const NevercIRAnalysisAPI *Analyses;
  uint64_t Reserved[2];
} NevercIRPassInvocation;
```

三个 API 指针随调用一起送达，所以 pass 主体不需要自己保存表。

通过 `OutPreserved` 报告哪些东西还成立：

```c
OutPreserved->Flags = NEVERC_IR_PRESERVE_ALL;   /* 或 _NONE、_CFG */
```

`NEVERC_IR_PRESERVE_CFG` 表示虽然指令变了但控制流图完好。自定义分析通过列进
`CustomAnalyses` 来保留。改过 IR 之后不要声明 `PRESERVE_ALL`——适配层会比对模
块代号并拒绝虚假声明。

函数 pass 和循环 pass 可能并发运行，因此可变的插件状态必须符合插件声明的
`NevercConcurrencyModel`。

## 分析

七个内建分析可按 ID 查询：`DOMINATOR_TREE`、`POST_DOMINATOR_TREE`、
`LOOP_INFO`、`SCALAR_EVOLUTION`、`MEMORY_SSA`、`CALL_GRAPH`、`ALIAS`。

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

每个分析都有带类型的访问器，而不是一团不透明数据：
`DominatorTreeDominates`、`GetLoopCount` / `GetLoopHeader` /
`GetLoopForBlock`、`GetScalarEvolutionConstantTripCount`、
`GetMemoryAccessKind`（`NONE`、`USE`、`DEF`、`PHI`、`LIVE_ON_ENTRY`）、
`GetDirectCalleeCount` / `GetDirectCallee`，以及 `Alias`（`NO`、`MAY`、
`PARTIAL`、`MUST`）。

插件分析连同自己的生命周期一起注册：

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

`Invalidate` 会被告知原因——`INVALIDATED_BY_PASS` 或
`INVALIDATED_BY_PLAN_DESTROY`。结果按调用缓存，并根据正在运行的 pass 保留了什
么来丢弃。依赖环在注册时就被拒绝，在分析回调内部修改 IR 也会被拒绝。

## 替换生成与优化

`NevercIRGenAPI` 替换 `neverc.ir.generate`：

```c
NevercIRGeneratePhaseInput In = {0};
In.Header = /* … */;
Gen->GetGeneratePhaseInput(Gen->Context, Frame, Frame->Input, &In);
/* In.SemanticUnit, .TargetTriple, .DataLayout, .SourceIdentity,
   .SourceDigest */

const NevercIRCoreAPI *Core;
const NevercIRBuilderAPI *Builders;
Gen->CreateModule(Gen->Context, Frame, SV("my.module"), &Core, &Builders);
/* … 构造模块 … */

NevercIRModuleArtifactDescriptor Descriptor = {0};
Descriptor.Header           = /* … */;
Descriptor.Product          = MyProductID;
Descriptor.DependencyDigest = Digest;
Gen->PublishModule(Gen->Context, Frame, &Descriptor, &Output);
```

`ImportModule` 可以从 bitcode 或文本 IR 开始，而不是从空模块开始。
`NevercIROptimizationAPI` 对 `neverc.ir.optimize` 是同样的形状，另外多了
`GetInputModule`（拿到传入模块）和 `RunBuiltinPipeline`（委托给内建流水线再对
其结果做后处理）。

两条路径都通过宿主发布而不是返回指针，都会校验目标兼容性，并且在发布失败时原子
地保留旧模块。之后 `neverc.ir.final_verify` 照常运行。

## 示例

| 文件 | 展示内容 |
|---|---|
| [`pluginsdk/examples/FunctionPass.c`] | 只读函数 pass，含 ABI 协商 |
| [`pluginsdk/examples/ExamplePlugin.c`] | 模块级 pass，用值游标遍历函数 |
| [`pluginsdk/examples/CustomCallConvPlugin.c`] | 属性与调用点性质 |

```sh
cmake --build build-neverc --target neverc-plugin-example-function-pass
build-neverc/bin/neverc \
  -fplugin=build-neverc/neverc/pluginsdk/examples/host/FunctionPass.so \
  -O2 -c input.c -o input.o
```

请使用 CMake 为你的平台生成的模块后缀。

## 规则

- 每个回调都要返回 `NevercStatus`。插件失败会变成结构化诊断；绝不要让异常越过
  C 边界。
- 在会被填充的调用之前，把输出结构清零并设置好 `Header`。
- 不要硬编码 opcode、类型或属性的数值。使用 [`PluginIRSchema.inc`] 里的名字，这
  样 schema 修订会变成编译错误。
- 每个 `BeginMutation` 恰好对应一次 `DestroyMutation`，每个 `CreateBuilder` 恰
  好对应一次 `DestroyBuilder`，错误路径上也一样。
- `ExportModule` 给你的东西要用 `ReleaseSerializedBuffer` 释放。
- 修改过 IR 之后绝不要声明 `NEVERC_IR_PRESERVE_ALL`。
- 除非插件声明了 `NEVERC_CONCURRENCY_SESSION_SERIAL`，否则要假定函数 pass 与循
  环 pass 是并行运行的。
- `neverc.ir.final_verify` 是 sealed 的。插件做什么都跳不过它。

规范性声明、schema 常量、阶段策略与测试证据分别见 [`PluginIR.h`]、
[`Schema/PluginIRSchema.inc`]、[`Schema/PhaseSchema.json`] 和 [`coverage.json`]。

<!-- reference links -->
[`coverage.json`]: coverage.json
[`PluginIR.h`]: ../../neverc/include/neverc/Plugin/PluginIR.h
[`PluginIRSchema.inc`]: ../../neverc/include/neverc/Plugin/Schema/PluginIRSchema.inc
[`pluginsdk/examples/CustomCallConvPlugin.c`]: ../../pluginsdk/examples/CustomCallConvPlugin.c
[`pluginsdk/examples/ExamplePlugin.c`]: ../../pluginsdk/examples/ExamplePlugin.c
[`pluginsdk/examples/FunctionPass.c`]: ../../pluginsdk/examples/FunctionPass.c
[`Schema/PhaseSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PhaseSchema.json
[`Schema/PluginIRSchema.inc`]: ../../neverc/include/neverc/Plugin/Schema/PluginIRSchema.inc
