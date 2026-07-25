**語言**: [English](mir.md) | [简体中文](mir.zh-CN.md) | [繁體中文](mir.zh-TW.md) | [日本語](mir.ja.md) | [한국어](mir.ko.md) | [Français](mir.fr.md) | [Deutsch](mir.de.md) | [Español](mir.es.md) | [Italiano](mir.it.md) | [Русский](mir.ru.md) | [العربية](mir.ar.md)

[← NeverC 外掛 ABI](README.zh-TW.md)

# NeverC 外掛 MIR API

`PluginMIR.h` 公開 Machine IR：機器函式、區塊、指令、運算元、虛擬與實體暫存器、
堆疊框架、常數池、跳躍表，以及記憶體運算元。外掛可以在九個穩定的程式碼產生掛鉤上
掛載 pass，或是整個取代 IR 到 MIR 的降階。

這裡有兩份 schema 交會。**通用 schema** 與目標無關，而且總是可用。任何與目標相關
的東西──真實的 opcode、暫存器編號、暫存器類別──都需要一份協商過的
**目標 schema**，而每一個需要它的值都會透過 `RequiresTargetSchema` 旗標明講。

## 介面

```c
#include "neverc/Plugin/PluginMIR.h"
```

| 介面 | 表 | 槽位 | 用途 |
|---|---|--:|---|
| `NEVERC_INTERFACE_MIR_{HIGH,LOW}` | `NevercMIRAPI` | 89 | 讀取與變更機器函式 |
| `NEVERC_INTERFACE_MIR_ANALYSIS_{HIGH,LOW}` | `NevercMIRAnalysisAPI` | 11 | 活躍性、支配關係、迴圈、壓力 |
| `NEVERC_INTERFACE_MIR_PASS_{HIGH,LOW}` | `NevercMIRPassAPI` | 1 | `RegisterPass` |
| `NEVERC_INTERFACE_MIR_PROVIDER_{HIGH,LOW}` | `NevercMIRProviderAPI` | 3 | 取代 IR → MIR 降階 |

四張表在 major 1 都是 `NEVERC_INTERFACE_STABLE`。請把回傳的 `TableSize` 和你會用
到的最後一個槽位的偏移量比對，並忽略較新的主機在它之後追加的任何東西。

## 階段

十個 MIR 階段，其中九個是 pass 掛鉤：

| 階段 | 時機 |
|---|---|
| `neverc.mir.pass.post_isel` | 指令選擇之後 |
| `neverc.mir.pass.post_legalize` | 合法化之後 |
| `neverc.mir.pass.pre_scheduler` | 排程之前 |
| `neverc.mir.pass.post_scheduler` | 排程之後 |
| `neverc.mir.pass.pre_regalloc` | 暫存器配置之前 |
| `neverc.mir.pass.post_regalloc` | 暫存器配置之後 |
| `neverc.mir.pass.post_prolog_epilog` | 插入序言／尾聲之後 |
| `neverc.mir.pass.preemit` | 就在發出之前 |
| `neverc.mir.pass.final` | 最後一個外掛槽 |
| `neverc.mir.final_verify` | **封印的** 主機 `MachineVerifier` |

九個掛鉤全都是 `OBSERVABLE | INTERCEPTABLE`。哪些分析存在取決於你掛在哪裡：活躍
區間在暫存器配置之前並不存在，而虛擬暫存器在配置之後就消失了。

`neverc.mir.final_verify` 會在最後一個外掛槽之後執行 LLVM 的
`MachineVerifier`。沒有任何外掛能停用、取代或跳過它。

## Schema

`Schema/PluginMIRSchema.inc` 是產生出來的，並由 `PluginMIR.h` 包含：

```c
#define NEVERC_MIR_SCHEMA_DIGEST          "6b523b20…"
#define NEVERC_MIR_ENTITY_COUNT           UINT32_C(4)
#define NEVERC_MIR_OPERAND_COUNT          UINT32_C(21)
#define NEVERC_MIR_GENERIC_OPCODE_COUNT   UINT32_C(266)
#define NEVERC_MIR_PROPERTY_COUNT         UINT32_C(11)
```

有四個呼叫在執行期描述這份 schema，每個都回傳一個 `NevercMIRSchemaEntry`，帶著
正規名稱、底層的 LLVM 值，以及是否需要目標 schema：

```c
NevercMIRSchemaEntry Entry = {0};
Entry.Header = /* … */;
MIR->GetGenericOpcodeInfo(MIR->Context, Opcode, &Entry);
/* Entry.StableID、.LLVMValue、.RequiresTargetSchema、.CanonicalName */
```

其餘三個是 `GetEntityInfo`、`GetOperandKindInfo` 與 `GetMachinePropertyInfo`。
`GetSchemaDigest` 回傳實際使用中那份對映的摘要──在信任任何目標相關的值之前，先
拿它和 `NEVERC_MIR_SCHEMA_DIGEST` 比對。

## 讀取 MIR

巡覽是雙向鏈結式的，而不是游標式的：

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
    /* Info.StableOpcode、.TargetOpcode、.RequiresTargetSchema、
       .IsBranch、.IsCall、.IsReturn、.IsTerminator、.IsBarrier、
       .IsInlineAssembly、.IsDebugInstruction、.IsPseudo、.IsBundle、
       .Flags、.OperandCount、.MemoryOperandCount                    */
    MIR->GetNextInstruction(MIR->Context, Task, Instruction, &Instruction);
  }
  MIR->GetNextBasicBlock(MIR->Context, Task, Block, &Block);
}
```

`CollectBasicBlocks` 與 `CollectInstructions` 則改成填一個有界陣列，而
`GetLastBasicBlock` / `GetPreviousInstruction` 可以往回走。CFG 查詢是
`GetSuccessorCount` / `GetSuccessor`（後者交出一個 `NevercMIRCFGEdge`，以分子／
分母對的形式帶著分支機率）、`GetPredecessorCount` / `GetPredecessor`，以及
`GetLiveInCount` / `GetLiveIn`。

指令旗標是那 18 個位元，從 `FRAME_SETUP` 與 `FRAME_DESTROY`，經過 fast-math 那一
組，到 `NO_MERGE`、`UNPREDICTABLE` 與 `NO_CONVERGENT`。

## 運算元

全部 21 種運算元種類都透過同一個帶標籤的聯集回來：

```c
NevercMIROperandValue Value = {0};
Value.Header = /* … */;
MIR->GetOperandValue(MIR->Context, Task, Operand, &Value);

switch (Value.Kind) {
case NEVERC_MIR_OPERAND_REGISTER:
  /* Value.Payload.Register.Number、.SubRegister、.Flags、.IsPhysical */
  break;
case NEVERC_MIR_OPERAND_IMMEDIATE:
  /* Value.Payload.Immediate */
  break;
case NEVERC_MIR_OPERAND_MACHINE_BASIC_BLOCK:
  /* Value.Payload.BasicBlock */
  break;
case NEVERC_MIR_OPERAND_GLOBAL_ADDRESS:
  /* Value.Payload.SymbolOffset.Symbol、.Offset */
  break;
}
```

這些種類是 `REGISTER`、`IMMEDIATE`、`C_IMMEDIATE`、`FP_IMMEDIATE`、
`MACHINE_BASIC_BLOCK`、`FRAME_INDEX`、`CONSTANT_POOL_INDEX`、`TARGET_INDEX`、
`JUMP_TABLE_INDEX`、`EXTERNAL_SYMBOL`、`GLOBAL_ADDRESS`、`BLOCK_ADDRESS`、
`REGISTER_MASK`、`REGISTER_LIVE_OUT`、`METADATA`、`MC_SYMBOL`、`CFI_INDEX`、
`INTRINSIC_ID`、`PREDICATE`、`SHUFFLE_MASK` 與 `DBG_INSTR_REF`。

暫存器運算元的旗標有 `DEF`、`IMPLICIT`、`KILL`、`DEAD`、`UNDEF`、
`EARLY_CLOBBER`、`RENAMABLE`、`INTERNAL_READ` 與 `DEBUG`。浮點立即值以
`NevercMIRWordView` 的形式抵達──小端序字組加上位元寬度，以及從 `IEEE_HALF` 到
`PPC_DOUBLE_DOUBLE` 這七種浮點語意之一──所以完全不涉及主機的浮點型別。

## 暫存器

一個虛擬暫存器由一個低階型別加上一份指派來描述：

```c
NevercMIRVirtualRegisterDesc Desc = {0};
Desc.Header             = /* … */;
Desc.AssignmentKind     = NEVERC_MIR_REG_ASSIGNMENT_CLASS;
Desc.TargetID           = RegisterClassID;   /* 需要目標 schema */
Desc.Type.Kind          = NEVERC_MIR_LLT_SCALAR;
Desc.Type.ScalarSizeInBits = 32;

uint32_t Register = 0;
MIR->CreateVirtualRegister(MIR->Context, Task, Mutation, &Desc, &Register);
```

指派種類有 `NONE`、`GENERIC`、`CLASS` 與 `BANK`；低階型別種類有 `INVALID`、
`SCALAR`、`POINTER`、`VECTOR` 與 `POINTER_VECTOR`，可伸縮向量則另有
`IsScalable`。

定義－使用查詢是 `GetRegisterDefCount` / `GetRegisterDef` 與
`GetRegisterUseCount` / `GetRegisterUse`；`ReplaceRegister` 會在一次暫存操作中改
寫每一處出現。函式層級的 live-in 會把一個實體暫存器和它被複製進去的虛擬暫存器配
成對（`GetFunctionLiveIn`、`AddFunctionLiveIn`、`RemoveFunctionLiveIn`），而區塊
層級的 live-in 則帶著一個 lane 遮罩（`AddBasicBlockLiveIn`、
`RemoveBasicBlockLiveIn`）。

## 堆疊框架

```c
int32_t FrameIndex = 0;
MIR->CreateStackObject(MIR->Context, Task, Mutation, /*Size=*/16,
                       /*Alignment=*/8, /*IsSpillSlot=*/NEVERC_FALSE,
                       /*StackID=*/0, &FrameIndex);
```

`CreateFixedStackObject` 會把物件放在一個已知位移處（帶 `IsImmutable` 與
`IsAliased`），而 `CreateVariableSizedStackObject` 處理動態配置。事後可以用
`SetFrameObjectSize`、`SetFrameObjectAlignment` 與 `SetFrameObjectOffset` 調整。

`NevercMIRFrameObjectInfo` 回報 `Index`、`Flags`、`Size`、`Offset`、`Alignment`
與 `StackID`；框架旗標有 `FIXED`、`SPILL_SLOT`、`VARIABLE_SIZED`、`IMMUTABLE`、
`ALIASED`、`DEAD` 與 `PREALLOCATED`。被呼叫者保存的狀態用 `GetCalleeSaved` 讀取，
並用 `SetCalleeSaved` 整批替換。

## 常數池、跳躍表、記憶體運算元

常數池項目以 `NevercMIRWordView` 攜帶它的值，所以整數項目和浮點項目用的是同一個
形狀：

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

跳躍表由一組目的地區塊建立，並使用七種項目種類之一（`BLOCK_ADDRESS`、
`GP_REL64_BLOCK_ADDRESS`、`GP_REL32_BLOCK_ADDRESS`、`LABEL_DIFFERENCE32`、
`LABEL_DIFFERENCE64`、`INLINE`、`CUSTOM32`）。

記憶體運算元是內容最豐富的描述子：旗標（`LOAD`、`STORE`、`VOLATILE`、
`NON_TEMPORAL`、`DEREFERENCEABLE`、`INVARIANT`，外加三個目標旗標）、大小與對齊、
九種之一的指標（`IR_VALUE`、`FIXED_STACK`、`STACK`、`CONSTANT_POOL`、
`JUMP_TABLE`、`GOT`、`UNKNOWN_STACK`、`TARGET_CUSTOM`、`UNKNOWN`）、成功與失敗
的原子序、一個同步範圍，以及 TBAA、alias-scope、no-alias 與 range 參照。用
`AddInstructionMemoryOperand` 把它掛上去。

## 交易式變更

每一次改動都暫存在一筆繫結到單一機器函式的變更裡：

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

提交會先跑一次結構性預檢，然後跑 Machine IR 驗證器。無效的運算元、壞掉的 CFG、
在目標 schema 要求真實 opcode 之處卻用了通用 opcode，或是不受支援的屬性宣稱，全
都會原子性地回捲。中止則會把區塊順序、指令、運算元、CFG 邊與機器屬性恢復成一模一
樣的原狀。

`EndMutation` 會釋放控制代碼，而且和 commit、abort 是分開的──兩條路徑上都要呼
叫它。

可暫存的操作有 `CreateBasicBlock`、`MoveBasicBlock`、`EraseBasicBlock`、
`CreateInstruction`、`MoveInstruction`、`EraseInstruction`、`AppendOperand`、
`SetOperandValue`、`SetInstructionFlags`、`AddCFGEdge`、`RemoveCFGEdge`，上面提
到的暫存器與框架呼叫、常數池與跳躍表呼叫、記憶體運算元呼叫，以及
`SetMachinePropertyWithProof`。

## 機器屬性需要證明

那十一個機器屬性──`IS_SSA`、`NO_PH_IS`、`TRACKS_LIVENESS`、`NO_V_REGS`、
`FAILED_I_SEL`、`LEGALIZED`、`REG_BANK_SELECTED`、`SELECTED`、
`TIED_OPS_REWRITTEN`、`FAILS_VERIFICATION` 與 `TRACKS_DEBUG_USER_VALUES`──可以
自由讀取，但絕不能自由設定：

```c
NevercMIRPropertyProof Proof = {0};
Proof.Header   = /* … */;
Proof.Property = NEVERC_MIR_PROPERTY_IS_SSA;
Proof.Kind     = NEVERC_MIR_PROPERTY_PROOF_INVALIDATION;
Proof.Value    = NEVERC_FALSE;
MIR->SetMachinePropertyWithProof(MIR->Context, Task, Mutation, &Proof);
```

證明有兩種。`INVALIDATION` 清掉一個其前提已被你的改動打破的屬性──這一種一定會被
接受，因為放棄一項保證是安全的。`STRUCTURAL_CHECK` 則要求主機在建立該屬性之前先
驗證它，所以宣稱 `IS_SSA` 要付出一次真正的檢查，而不只是一句承諾。

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

那就是 `pluginsdk/examples/MachinePass.c` 的原文。層級有 `MODULE`、`FUNCTION`
與 `BASIC_BLOCK`。`RequiredAnalyses` 與 `PreservedAnalyses` 是
`NevercMIRBuiltinAnalysis` 的陣列，而 `RequiredTargetSchemaDigest` 會讓這個 pass
拒絕在它並非為之建置的 schema 上執行。

呼叫會帶著 `Task`、`Phase`、`PassID`、`Level`、在該層級有效的 `Function` 與
`BasicBlock`、`Core` 與 `Analyses` 這兩張表，以及當前作用中的
`TargetSchemaDigest`。

透過 `OutPreserved` 回報保留情形──`NEVERC_MIR_PRESERVE_NONE`、`_CFG` 或
`_ALL`，外加 `Analyses` 中的一份明確清單。在已提交的變更之後宣稱 `PRESERVE_ALL`
會被拒絕。

函式 pass 可能在並行的程式碼產生分區中執行；模組層級的 pass 則在序列化的流水線屏
障處執行。外掛宣告的並行與可重入模型仍然管轄你自己的狀態。

## 分析

六個內建分析：`LIVE_INTERVALS`、`LIVE_VARIABLES`、`SLOT_INDEXES`、
`DOMINATOR_TREE`、`LOOP_INFO` 與 `REGISTER_PRESSURE`。

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
  /* Segment.Start、Segment.End */
}
```

另外還有：`DominatorTreeDominates`、`GetLoopCount` / `GetLoopHeader` /
`GetLoopForBlock`、`GetSlotIndex`、`IsRegisterLiveInBlock`，以及
`GetRegisterPressureSetCount` / `GetRegisterPressure`。

可用性取決於掛鉤位置。在 `post_isel` 索取活躍區間會以
`NEVERC_STATUS_CAPABILITY_UNAVAILABLE` 失敗，因為底層的 LLVM 分析那時還不存在。
已提交的變更會讓它所影響的結果控制代碼失效。

## 取代 IR 到 MIR 的降階

```c
NevercIRToMIRInputInfo In = {0};
In.Header = /* … */;
Provider->GetIRToMIRInput(Provider->Context, Frame, Frame->Input, &In);
/* In.Module、.IR、.TargetID、.CompatibilityKey、.TargetSchemaDigest、
   .DefinedFunctionCount */

const NevercMIRAPI *MIR;
NevercMachineFunctionHandle MF;
Provider->GetOrCreateMachineFunction(Provider->Context, Frame, IRFunction,
                                     &MIR, &MF);
/* … 建構機器函式 … */

NevercMIRModuleCoverageDescriptor Coverage = {0};
Coverage.Header              = /* … */;
Coverage.HandlesGlobals      = NEVERC_TRUE;
Coverage.HandlesConstructors = NEVERC_TRUE;
Coverage.HandlesDebugInfo    = NEVERC_FALSE;
Coverage.HandlesUnwind       = NEVERC_FALSE;
Provider->PublishMIRModule(Provider->Context, Frame, &Coverage, &Output);
```

涵蓋範圍描述子就是部分實作的 Provider 保持誠實的方式：只宣告你真的降階了的部分，
主機就會自己處理其餘的，而不是默默丟掉全域變數、建構器、除錯資訊或展開表。

## 範例

```sh
cmake --build build-neverc --target neverc-plugin-example-machine-pass
build-neverc/bin/neverc \
  -fplugin=build-neverc/neverc/pluginsdk/examples/host/MachinePass.so \
  -O2 -fno-lto -c input.c -o input.o
```

請使用 CMake 為你的平台產生的模組副檔名。

## 規則

- 回呼回傳之後，不要保留任務控制代碼、MIR 控制代碼或借用的 view，也絕不要憑空捏造
  控制代碼值或 LLVM 的 opcode 編號。
- 在使用任何帶有 `RequiresTargetSchema` 旗標的值之前，先把 `GetSchemaDigest` 和
  你編譯進去的摘要比對。
- 只在變更裡面做修改。每個 `BeginMutation` 都恰好對到一個 `EndMutation`，在
  commit 或 abort 之後呼叫。
- 沒有證明就不要宣稱機器屬性；當你的改動放棄了某項保證時，優先用 `INVALIDATION`
  而不是 `STRUCTURAL_CHECK`。
- 在已提交的變更之後，絕不要宣稱 `NEVERC_MIR_PRESERVE_ALL`。
- 確認你需要的分析在你選的掛鉤上真的可用。
- 初始化每一個表頭與保留欄位；狀態要跨 C 邊界回傳，並且絕不讓 C++ 例外穿過去。
- `neverc.mir.final_verify` 是封印的。不管怎樣它都會執行。

規範性宣告、schema 常數、階段政策與涵蓋證據，請見 `PluginMIR.h`、
`Schema/PluginMIRSchema.inc`、`Schema/PhaseSchema.json` 與 `coverage.json`。
