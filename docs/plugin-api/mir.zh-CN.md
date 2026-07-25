**语言**: [English](mir.md) | [简体中文](mir.zh-CN.md) | [繁體中文](mir.zh-TW.md) | [日本語](mir.ja.md) | [한국어](mir.ko.md) | [Français](mir.fr.md) | [Deutsch](mir.de.md) | [Español](mir.es.md) | [Italiano](mir.it.md) | [Русский](mir.ru.md) | [العربية](mir.ar.md)

[← NeverC 插件 ABI](README.zh-CN.md)

# NeverC 插件 MIR API

[`PluginMIR.h`] 暴露 Machine IR：机器函数、基本块、指令、操作数、虚拟与物理寄存
器、栈帧、常量池、跳转表和内存操作数。插件可以在九个稳定的代码生成钩子上挂载
pass，或者整个替换掉 IR 到 MIR 的降级。

这里有两套 schema 交汇。**通用 schema** 与目标无关，始终可用。任何与目标相关的
东西——真实 opcode、寄存器号、寄存器类——都需要协商过的**目标 schema**，而每
个需要它的值都会通过 `RequiresTargetSchema` 标志说明这一点。

## 接口

```c
#include "neverc/Plugin/PluginMIR.h"
```

| 接口 | 表 | 槽位 | 用途 |
|---|---|--:|---|
| `NEVERC_INTERFACE_MIR_{HIGH,LOW}` | `NevercMIRAPI` | 89 | 读取并修改机器函数 |
| `NEVERC_INTERFACE_MIR_ANALYSIS_{HIGH,LOW}` | `NevercMIRAnalysisAPI` | 11 | 活跃性、支配树、循环、寄存器压力 |
| `NEVERC_INTERFACE_MIR_PASS_{HIGH,LOW}` | `NevercMIRPassAPI` | 1 | `RegisterPass` |
| `NEVERC_INTERFACE_MIR_PROVIDER_{HIGH,LOW}` | `NevercMIRProviderAPI` | 3 | 替换 IR → MIR 的降级 |

四个都是 major 1 的 `NEVERC_INTERFACE_STABLE`。检查返回的 `TableSize` 是否覆盖
到你要用的最后一个槽位，并忽略更新宿主追加在其后的任何内容。

## 阶段

十个 MIR 阶段，其中九个是 pass 钩子：

| 阶段 | 时机 |
|---|---|
| `neverc.mir.pass.post_isel` | 指令选择之后 |
| `neverc.mir.pass.post_legalize` | 合法化之后 |
| `neverc.mir.pass.pre_scheduler` | 调度之前 |
| `neverc.mir.pass.post_scheduler` | 调度之后 |
| `neverc.mir.pass.pre_regalloc` | 寄存器分配之前 |
| `neverc.mir.pass.post_regalloc` | 寄存器分配之后 |
| `neverc.mir.pass.post_prolog_epilog` | 序言／尾声插入之后 |
| `neverc.mir.pass.preemit` | 发射之前 |
| `neverc.mir.pass.final` | 最后一个插件槽位 |
| `neverc.mir.final_verify` | **Sealed** 宿主 `MachineVerifier` |

九个钩子都是 `OBSERVABLE | INTERCEPTABLE`。哪些分析可用取决于你挂在哪里：寄存
器分配之前没有 live interval，之后虚拟寄存器已经不存在了。

`neverc.mir.final_verify` 在最后一个插件槽位之后运行 LLVM 的
`MachineVerifier`。没有插件能禁用、替换或跳过它。

## Schema

[`Schema/PluginMIRSchema.inc`] 由工具生成，并由 [`PluginMIR.h`] 包含：

```c
#define NEVERC_MIR_SCHEMA_DIGEST          "6b523b20…"
#define NEVERC_MIR_ENTITY_COUNT           UINT32_C(4)
#define NEVERC_MIR_OPERAND_COUNT          UINT32_C(21)
#define NEVERC_MIR_GENERIC_OPCODE_COUNT   UINT32_C(266)
#define NEVERC_MIR_PROPERTY_COUNT         UINT32_C(11)
```

四个调用在运行时描述 schema，每个都返回一个 `NevercMIRSchemaEntry`，包含规范名
称、底层 LLVM 值，以及是否需要目标 schema：

```c
NevercMIRSchemaEntry Entry = {0};
Entry.Header = /* … */;
MIR->GetGenericOpcodeInfo(MIR->Context, Opcode, &Entry);
/* Entry.StableID, .LLVMValue, .RequiresTargetSchema, .CanonicalName */
```

另外三个是 `GetEntityInfo`、`GetOperandKindInfo`、`GetMachinePropertyInfo`。
`GetSchemaDigest` 返回实际生效的映射摘要——在信任任何目标相关的值之前，先拿它
和 `NEVERC_MIR_SCHEMA_DIGEST` 比对。

## 读取 MIR

遍历用的是双向链表而不是游标：

```c
NevercMachineBasicBlockHandle Block;
MIR->GetFirstBasicBlock(MIR->Context, Task, Function, &Block);

while (!neverc_handle_is_null(Block)) {
  NevercMachineInstrHandle Instruction;
  MIR->GetFirstInstruction(MIR->Context, Task, Block, &Instruction);

  while (!neverc_handle_is_null(Instruction)) {
    NevercMIRInstructionInfo Info = {0};
    Info.Header = (NevercABITableHeader){sizeof(Info), NEVERC_MIR_API_MAJOR,
                                         NEVERC_MIR_API_MINOR, 0};
    MIR->GetInstructionInfo(MIR->Context, Task, Instruction, &Info);
    /* Info.StableOpcode, .TargetOpcode, .RequiresTargetSchema,
       .IsBranch, .IsCall, .IsReturn, .IsTerminator, .IsBarrier,
       .IsInlineAssembly, .IsDebugInstruction, .IsPseudo, .IsBundle,
       .Flags, .OperandCount, .MemoryOperandCount                    */
    MIR->GetNextInstruction(MIR->Context, Task, Instruction, &Instruction);
  }
  MIR->GetNextBasicBlock(MIR->Context, Task, Block, &Block);
}
```

`CollectBasicBlocks` 和 `CollectInstructions` 则填充一个有界数组；
`GetLastBasicBlock` / `GetPreviousInstruction` 反向遍历。CFG 查询是
`GetSuccessorCount` / `GetSuccessor`（返回 `NevercMIRCFGEdge`，分支概率以分子／
分母对表示）、`GetPredecessorCount` / `GetPredecessor`，以及
`GetLiveInCount` / `GetLiveIn`。

指令标志是 18 个位，从 `FRAME_SETUP`、`FRAME_DESTROY` 经过 fast-math 一组，一
直到 `NO_MERGE`、`UNPREDICTABLE`、`NO_CONVERGENT`。

## 操作数

全部 21 种操作数通过同一个带标签联合返回：

```c
NevercMIROperandValue Value = {0};
Value.Header = /* … */;
MIR->GetOperandValue(MIR->Context, Task, Operand, &Value);

switch (Value.Kind) {
case NEVERC_MIR_OPERAND_REGISTER:
  /* Value.Payload.Register.Number, .SubRegister, .Flags, .IsPhysical */
  break;
case NEVERC_MIR_OPERAND_IMMEDIATE:
  /* Value.Payload.Immediate */
  break;
case NEVERC_MIR_OPERAND_MACHINE_BASIC_BLOCK:
  /* Value.Payload.BasicBlock */
  break;
case NEVERC_MIR_OPERAND_GLOBAL_ADDRESS:
  /* Value.Payload.SymbolOffset.Symbol, .Offset */
  break;
}
```

这些种类是 `REGISTER`、`IMMEDIATE`、`C_IMMEDIATE`、`FP_IMMEDIATE`、
`MACHINE_BASIC_BLOCK`、`FRAME_INDEX`、`CONSTANT_POOL_INDEX`、`TARGET_INDEX`、
`JUMP_TABLE_INDEX`、`EXTERNAL_SYMBOL`、`GLOBAL_ADDRESS`、`BLOCK_ADDRESS`、
`REGISTER_MASK`、`REGISTER_LIVE_OUT`、`METADATA`、`MC_SYMBOL`、`CFI_INDEX`、
`INTRINSIC_ID`、`PREDICATE`、`SHUFFLE_MASK`、`DBG_INSTR_REF`。

寄存器操作数标志是 `DEF`、`IMPLICIT`、`KILL`、`DEAD`、`UNDEF`、
`EARLY_CLOBBER`、`RENAMABLE`、`INTERNAL_READ`、`DEBUG`。浮点立即数以
`NevercMIRWordView` 形式送达——小端字加上位宽，以及从 `IEEE_HALF` 到
`PPC_DOUBLE_DOUBLE` 的七种浮点语义之一——所以完全不涉及宿主的浮点类型。

## 寄存器

一个虚拟寄存器由低级类型加上一个分配方式描述：

```c
NevercMIRVirtualRegisterDesc Desc = {0};
Desc.Header             = /* … */;
Desc.AssignmentKind     = NEVERC_MIR_REG_ASSIGNMENT_CLASS;
Desc.TargetID           = RegisterClassID;   /* 需要目标 schema */
Desc.Type.Kind          = NEVERC_MIR_LLT_SCALAR;
Desc.Type.ScalarSizeInBits = 32;

uint32_t Register = 0;
MIR->CreateVirtualRegister(MIR->Context, Task, Mutation, &Desc, &Register);
```

分配方式有 `NONE`、`GENERIC`、`CLASS`、`BANK`；低级类型种类有 `INVALID`、
`SCALAR`、`POINTER`、`VECTOR`、`POINTER_VECTOR`，可伸缩向量用 `IsScalable`。

def-use 查询是 `GetRegisterDefCount` / `GetRegisterDef` 与
`GetRegisterUseCount` / `GetRegisterUse`；`ReplaceRegister` 在一次暂存操作里改
写所有出现处。函数级 live-in 把物理寄存器与它被拷入的虚拟寄存器配对
（`GetFunctionLiveIn`、`AddFunctionLiveIn`、`RemoveFunctionLiveIn`），块级
live-in 则带 lane mask（`AddBasicBlockLiveIn`、`RemoveBasicBlockLiveIn`）。

## 栈帧

```c
int32_t FrameIndex = 0;
MIR->CreateStackObject(MIR->Context, Task, Mutation, /*Size=*/16,
                       /*Alignment=*/8, /*IsSpillSlot=*/NEVERC_FALSE,
                       /*StackID=*/0, &FrameIndex);
```

`CreateFixedStackObject` 把对象放在已知偏移处（带 `IsImmutable` 和
`IsAliased`），`CreateVariableSizedStackObject` 处理动态分配。事后可用
`SetFrameObjectSize`、`SetFrameObjectAlignment`、`SetFrameObjectOffset` 调整。

`NevercMIRFrameObjectInfo` 报告 `Index`、`Flags`、`Size`、`Offset`、
`Alignment`、`StackID`；栈帧标志是 `FIXED`、`SPILL_SLOT`、`VARIABLE_SIZED`、
`IMMUTABLE`、`ALIASED`、`DEAD`、`PREALLOCATED`。被调用者保存的状态用
`GetCalleeSaved` 读取，用 `SetCalleeSaved` 整体替换。

## 常量池、跳转表、内存操作数

常量池条目以 `NevercMIRWordView` 携带取值，所以整数条目和浮点条目形状一致：

```c
NevercMIRConstantPoolEntryDesc Desc = {0};
Desc.Header       = /* … */;
Desc.Kind         = NEVERC_MIR_CONSTANT_INTEGER;
Desc.Alignment    = 8;
Desc.Value.Data   = Words;
Desc.Value.Count  = 1;
Desc.Value.BitWidth = 64;

uint32_t Index = 0;
MIR->CreateConstantPoolEntry(MIR->Context, Task, Mutation, &Desc, &Index);
```

跳转表由一组目标块创建，可选七种条目形式之一（`BLOCK_ADDRESS`、
`GP_REL64_BLOCK_ADDRESS`、`GP_REL32_BLOCK_ADDRESS`、`LABEL_DIFFERENCE32`、
`LABEL_DIFFERENCE64`、`INLINE`、`CUSTOM32`）。

内存操作数是最复杂的描述符：标志（`LOAD`、`STORE`、`VOLATILE`、
`NON_TEMPORAL`、`DEREFERENCEABLE`、`INVARIANT`，外加三个目标标志）、大小与对
齐、九种之一的指针（`IR_VALUE`、`FIXED_STACK`、`STACK`、`CONSTANT_POOL`、
`JUMP_TABLE`、`GOT`、`UNKNOWN_STACK`、`TARGET_CUSTOM`、`UNKNOWN`）、成功与失败
的原子序、同步作用域，以及 TBAA、alias scope、no-alias 和 range 引用。用
`AddInstructionMemoryOperand` 挂载。

## 事务式修改

每一处改动都暂存在绑定到单个机器函数的 mutation 里：

```c
NevercMIRMutationHandle Mutation;
MIR->BeginMutation(MIR->Context, Task, Function, &Mutation);

NevercMIRInstructionOpcode Opcode = {0};
Opcode.StableOpcode = MyGenericOpcode;

NevercMachineInstrHandle New;
MIR->CreateInstruction(MIR->Context, Task, Mutation, Block,
                       /*InsertBefore=*/Terminator, Opcode, &New);

NevercMIROperandValue Op = {0};
Op.Header = /* … */;
Op.Kind   = NEVERC_MIR_OPERAND_IMMEDIATE;
Op.Payload.Immediate = 42;
MIR->AppendOperand(MIR->Context, Task, Mutation, New, &Op, &Operand);

Status = MIR->CommitMutation(MIR->Context, Task, Mutation);
if (Status.Code != NEVERC_STATUS_OK)
  MIR->AbortMutation(MIR->Context, Task, Mutation);
MIR->EndMutation(MIR->Context, Task, Mutation);
```

提交会先做结构预检，再跑 Machine IR 验证器。非法操作数、损坏的 CFG、在目标
schema 要求真实 opcode 处使用通用 opcode，以及不成立的属性声明，都会被原子回
滚。中止则把块顺序、指令、操作数、CFG 边和机器属性精确还原。

`EndMutation` 释放句柄，与 commit、abort 相互独立——两条路径上都要调用它。

暂存操作包括 `CreateBasicBlock`、`MoveBasicBlock`、`EraseBasicBlock`、
`CreateInstruction`、`MoveInstruction`、`EraseInstruction`、`AppendOperand`、
`SetOperandValue`、`SetInstructionFlags`、`AddCFGEdge`、`RemoveCFGEdge`，上面提
到的寄存器与栈帧调用，常量池与跳转表调用，内存操作数调用，以及
`SetMachinePropertyWithProof`。

## 机器属性需要证明

十一个机器属性——`IS_SSA`、`NO_PH_IS`、`TRACKS_LIVENESS`、`NO_V_REGS`、
`FAILED_I_SEL`、`LEGALIZED`、`REG_BANK_SELECTED`、`SELECTED`、
`TIED_OPS_REWRITTEN`、`FAILS_VERIFICATION`、`TRACKS_DEBUG_USER_VALUES`——可以
随意读取，但不能随意设置：

```c
NevercMIRPropertyProof Proof = {0};
Proof.Header   = /* … */;
Proof.Property = NEVERC_MIR_PROPERTY_IS_SSA;
Proof.Kind     = NEVERC_MIR_PROPERTY_PROOF_INVALIDATION;
Proof.Value    = NEVERC_FALSE;
MIR->SetMachinePropertyWithProof(MIR->Context, Task, Mutation, &Proof);
```

证明分两种。`INVALIDATION` 清除一个因你的改动而前提不再成立的属性——这总是被
接受的，因为放弃一项保证是安全的。`STRUCTURAL_CHECK` 要求宿主在建立该属性之前
先验证它，所以声明 `IS_SSA` 的代价是一次真实检查，而不是一句空口承诺。

## Pass

```c
NevercMIRPassDescriptor Pass = {0};
Pass.Header = (NevercABITableHeader){sizeof(Pass), NEVERC_MIR_PASS_API_MAJOR,
                                     NEVERC_MIR_PASS_API_MINOR, 0};
Pass.PassID        = SV("example.machine-pass");
Pass.Phase         = (NevercInterfaceID){NEVERC_PHASE_MIR_PASS_PREEMIT_HIGH,
                                         NEVERC_PHASE_MIR_PASS_PREEMIT_LOW};
Pass.Level         = NEVERC_MIR_PASS_LEVEL_FUNCTION;
Pass.Deterministic = NEVERC_TRUE;
Pass.Run           = run_machine_function;
PassAPI->RegisterPass(PassAPI->Context, RegistrarContext, &Pass);
```

这就是 [`pluginsdk/examples/MachinePass.c`] 的原文。层级有 `MODULE`、
`FUNCTION`、`BASIC_BLOCK`。`RequiredAnalyses` 和 `PreservedAnalyses` 是
`NevercMIRBuiltinAnalysis` 数组，而 `RequiredTargetSchemaDigest` 会让这个 pass
拒绝在它并非为之构建的 schema 上运行。

调用信息携带 `Task`、`Phase`、`PassID`、`Level`、该层级下有效的 `Function` 与
`BasicBlock`、`Core` 与 `Analyses` 两张表，以及当前生效的
`TargetSchemaDigest`。

通过 `OutPreserved` 报告保留情况——`NEVERC_MIR_PRESERVE_NONE`、`_CFG` 或
`_ALL`，另可在 `Analyses` 中给出显式列表。提交过 mutation 之后再声明
`PRESERVE_ALL` 会被拒绝。

函数 pass 可能在并行的代码生成分区里运行；模块级 pass 在串行化的流水线屏障处运
行。插件声明的并发与可重入模型仍然约束你自己的状态。

## 分析

六个内建分析：`LIVE_INTERVALS`、`LIVE_VARIABLES`、`SLOT_INDEXES`、
`DOMINATOR_TREE`、`LOOP_INFO`、`REGISTER_PRESSURE`。

```c
NevercMIRAnalysisResultHandle Intervals;
Analyses->QueryBuiltin(Analyses->Context, Task,
                       NEVERC_MIR_ANALYSIS_LIVE_INTERVALS, Function,
                       &Intervals);

uint64_t SegmentCount = 0;
Analyses->GetLiveIntervalSegmentCount(Analyses->Context, Task, Intervals,
                                      Register, &SegmentCount);
for (uint64_t I = 0; I != SegmentCount; ++I) {
  NevercMIRLiveRangeSegment Segment;
  Analyses->GetLiveIntervalSegment(Analyses->Context, Task, Intervals,
                                   Register, I, &Segment);
  /* Segment.Start, Segment.End */
}
```

另有 `DominatorTreeDominates`、`GetLoopCount` / `GetLoopHeader` /
`GetLoopForBlock`、`GetSlotIndex`、`IsRegisterLiveInBlock`，以及
`GetRegisterPressureSetCount` / `GetRegisterPressure`。

可用性取决于钩子位置。在 `post_isel` 处请求 live interval 会以
`NEVERC_STATUS_CAPABILITY_UNAVAILABLE` 失败，因为底层的 LLVM 分析那时还不存
在。一次已提交的 mutation 会让它影响到的结果句柄失效。

## 替换 IR 到 MIR 的降级

```c
NevercIRToMIRInputInfo In = {0};
In.Header = /* … */;
Provider->GetIRToMIRInput(Provider->Context, Frame, Frame->Input, &In);
/* In.Module, .IR, .TargetID, .CompatibilityKey, .TargetSchemaDigest,
   .DefinedFunctionCount */

const NevercMIRAPI *MIR;
NevercMachineFunctionHandle MF;
Provider->GetOrCreateMachineFunction(Provider->Context, Frame, IRFunction,
                                     &MIR, &MF);
/* … 构造机器函数 … */

NevercMIRModuleCoverageDescriptor Coverage = {0};
Coverage.Header              = /* … */;
Coverage.HandlesGlobals      = NEVERC_TRUE;
Coverage.HandlesConstructors = NEVERC_TRUE;
Coverage.HandlesDebugInfo    = NEVERC_FALSE;
Coverage.HandlesUnwind       = NEVERC_FALSE;
Provider->PublishMIRModule(Provider->Context, Frame, &Coverage, &Output);
```

覆盖描述符是部分实现的 Provider 保持诚实的方式：只声明你确实降级了的部分，宿主
就会自己处理其余部分，而不是悄悄丢掉全局变量、构造函数、调试信息或展开表。

## 示例

```sh
cmake --build build-neverc --target neverc-plugin-example-machine-pass
build-neverc/bin/neverc \
  -fplugin=build-neverc/neverc/pluginsdk/examples/host/MachinePass.so \
  -O2 -fno-lto -c input.c -o input.o
```

请使用 CMake 为你的平台生成的模块后缀。

## 规则

- 回调返回后不要再持有任务句柄、MIR 句柄或借来的视图，也不要自己编造句柄值或
  LLVM opcode 号。
- 在消费任何带 `RequiresTargetSchema` 标志的值之前，先用 `GetSchemaDigest` 与
  你编译时的摘要比对。
- 只能在 mutation 内部修改。每个 `BeginMutation` 在 commit 或 abort 之后都要恰
  好对应一次 `EndMutation`。
- 没有证明就不要声明机器属性；当你的改动让某项属性不再成立时，优先用
  `INVALIDATION` 而不是 `STRUCTURAL_CHECK`。
- 提交过 mutation 之后绝不要声明 `NEVERC_MIR_PRESERVE_ALL`。
- 确认你需要的分析在你选的钩子处确实可用。
- 初始化每个表头和保留字段；跨 C 边界返回状态码，绝不让 C++ 异常穿过。
- `neverc.mir.final_verify` 是 sealed 的，无论如何都会运行。

规范性声明、schema 常量、阶段策略与覆盖证据分别见 [`PluginMIR.h`]、
[`Schema/PluginMIRSchema.inc`]、[`Schema/PhaseSchema.json`] 和 [`coverage.json`]。

<!-- reference links -->
[`coverage.json`]: coverage.json
[`PluginMIR.h`]: ../../neverc/include/neverc/Plugin/PluginMIR.h
[`pluginsdk/examples/MachinePass.c`]: ../../pluginsdk/examples/MachinePass.c
[`Schema/PhaseSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PhaseSchema.json
[`Schema/PluginMIRSchema.inc`]: ../../neverc/include/neverc/Plugin/Schema/PluginMIRSchema.inc
