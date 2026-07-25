**언어**: [English](mir.md) | [简体中文](mir.zh-CN.md) | [繁體中文](mir.zh-TW.md) | [日本語](mir.ja.md) | [한국어](mir.ko.md) | [Français](mir.fr.md) | [Deutsch](mir.de.md) | [Español](mir.es.md) | [Italiano](mir.it.md) | [Русский](mir.ru.md) | [العربية](mir.ar.md)

[← NeverC 플러그인 ABI](README.ko.md)

# NeverC 플러그인 MIR API

[`PluginMIR.h`] 는 Machine IR 을 공개합니다. 머신 함수, 블록, 명령어, 오퍼랜드,
가상 및 물리 레지스터, 스택 프레임, 상수 풀, 점프 테이블, 메모리 오퍼랜드가
그것입니다. 플러그인은 아홉 개의 안정된 코드 생성 훅에 패스를 붙이거나, IR →
MIR 하강을 통째로 대체할 수 있습니다.

여기서 두 개의 스키마가 만납니다. **범용 스키마** 는 타깃에 독립적이고 언제나
쓸 수 있습니다. 타깃에 종속된 것 — 실제 옵코드, 레지스터 번호, 레지스터 클래스 —
은 협상된 **타깃 스키마** 를 요구하며, 그것이 필요한 값은 모두
`RequiresTargetSchema` 플래그로 그렇다고 밝힙니다.

## 인터페이스

```c
#include "neverc/Plugin/PluginMIR.h"
```

| 인터페이스 | 테이블 | 슬롯 | 목적 |
|---|---|--:|---|
| `NEVERC_INTERFACE_MIR_{HIGH,LOW}` | `NevercMIRAPI` | 89 | 머신 함수 읽기와 변경 |
| `NEVERC_INTERFACE_MIR_ANALYSIS_{HIGH,LOW}` | `NevercMIRAnalysisAPI` | 11 | 생존성, 지배 관계, 루프, 레지스터 압력 |
| `NEVERC_INTERFACE_MIR_PASS_{HIGH,LOW}` | `NevercMIRPassAPI` | 1 | `RegisterPass` |
| `NEVERC_INTERFACE_MIR_PROVIDER_{HIGH,LOW}` | `NevercMIRProviderAPI` | 3 | IR → MIR 하강을 대체 |

네 개 모두 major 1 에서 `NEVERC_INTERFACE_STABLE` 입니다. 반환된 `TableSize` 를
여러분이 쓰는 마지막 슬롯의 오프셋과 견주어 확인하고, 새 호스트가 그 뒤에 덧붙인
것은 무시하십시오.

## 단계

MIR 단계는 열 개이며, 그중 아홉이 패스 훅입니다:

| 단계 | 시점 |
|---|---|
| `neverc.mir.pass.post_isel` | 명령어 선택 이후 |
| `neverc.mir.pass.post_legalize` | 합법화 이후 |
| `neverc.mir.pass.pre_scheduler` | 스케줄링 이전 |
| `neverc.mir.pass.post_scheduler` | 스케줄링 이후 |
| `neverc.mir.pass.pre_regalloc` | 레지스터 할당 이전 |
| `neverc.mir.pass.post_regalloc` | 레지스터 할당 이후 |
| `neverc.mir.pass.post_prolog_epilog` | 프롤로그/에필로그 삽입 이후 |
| `neverc.mir.pass.preemit` | 방출 직전 |
| `neverc.mir.pass.final` | 마지막 플러그인 슬롯 |
| `neverc.mir.final_verify` | **봉인된** 호스트 `MachineVerifier` |

아홉 개의 훅은 모두 `OBSERVABLE | INTERCEPTABLE` 입니다. 어떤 분석이 존재하는지는
어디에 붙느냐에 달려 있습니다. 생존 구간은 레지스터 할당 이전에는 없고, 가상
레지스터는 할당 이후에는 사라집니다.

`neverc.mir.final_verify` 는 마지막 플러그인 슬롯 뒤에 LLVM 의
`MachineVerifier` 를 실행합니다. 어떤 플러그인도 이를 끄거나 대체하거나 건너뛸 수
없습니다.

## 스키마

[`Schema/PluginMIRSchema.inc`] 는 생성물이며 [`PluginMIR.h`] 가 포함합니다:

```c
#define NEVERC_MIR_SCHEMA_DIGEST          "6b523b20…"
#define NEVERC_MIR_ENTITY_COUNT           UINT32_C(4)
#define NEVERC_MIR_OPERAND_COUNT          UINT32_C(21)
#define NEVERC_MIR_GENERIC_OPCODE_COUNT   UINT32_C(266)
#define NEVERC_MIR_PROPERTY_COUNT         UINT32_C(11)
```

네 개의 호출이 런타임에 스키마를 서술하며, 각각 정규 이름과 바탕이 되는 LLVM 값,
그리고 타깃 스키마가 필요한지를 담은 `NevercMIRSchemaEntry` 를 반환합니다:

```c
NevercMIRSchemaEntry Entry = {0};
Entry.Header = /* … */;
MIR->GetGenericOpcodeInfo(MIR->Context, Opcode, &Entry);
/* Entry.StableID, .LLVMValue, .RequiresTargetSchema, .CanonicalName */
```

나머지는 `GetEntityInfo`, `GetOperandKindInfo`, `GetMachinePropertyInfo` 입니다.
`GetSchemaDigest` 는 실제로 쓰이는 매핑의 다이제스트를 돌려줍니다 — 타깃에 종속된
값을 믿기 전에 `NEVERC_MIR_SCHEMA_DIGEST` 와 견주어 보십시오.

## MIR 읽기

순회는 커서 방식이 아니라 이중 연결 방식입니다:

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

`CollectBasicBlocks` 와 `CollectInstructions` 는 대신 경계가 있는 배열을 채우고,
`GetLastBasicBlock` / `GetPreviousInstruction` 은 뒤로 걷습니다. CFG 질의는
`GetSuccessorCount` / `GetSuccessor`(후자는 분기 확률을 분자/분모 쌍으로 나르는
`NevercMIRCFGEdge` 를 내놓습니다), `GetPredecessorCount` / `GetPredecessor`,
`GetLiveInCount` / `GetLiveIn` 입니다.

명령어 플래그는 `FRAME_SETUP` 과 `FRAME_DESTROY` 부터 fast-math 무리를 거쳐
`NO_MERGE`, `UNPREDICTABLE`, `NO_CONVERGENT` 까지 18개 비트입니다.

## 오퍼랜드

21가지 오퍼랜드 종류가 모두 하나의 태그 붙은 공용체로 돌아옵니다:

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

종류는 `REGISTER`, `IMMEDIATE`, `C_IMMEDIATE`, `FP_IMMEDIATE`,
`MACHINE_BASIC_BLOCK`, `FRAME_INDEX`, `CONSTANT_POOL_INDEX`, `TARGET_INDEX`,
`JUMP_TABLE_INDEX`, `EXTERNAL_SYMBOL`, `GLOBAL_ADDRESS`, `BLOCK_ADDRESS`,
`REGISTER_MASK`, `REGISTER_LIVE_OUT`, `METADATA`, `MC_SYMBOL`, `CFI_INDEX`,
`INTRINSIC_ID`, `PREDICATE`, `SHUFFLE_MASK`, `DBG_INSTR_REF` 입니다.

레지스터 오퍼랜드 플래그는 `DEF`, `IMPLICIT`, `KILL`, `DEAD`, `UNDEF`,
`EARLY_CLOBBER`, `RENAMABLE`, `INTERNAL_READ`, `DEBUG` 입니다. 부동소수점
즉치값은 `NevercMIRWordView` 로 도착합니다 — 리틀엔디언 워드에 비트 폭과,
`IEEE_HALF` 부터 `PPC_DOUBLE_DOUBLE` 까지 일곱 가지 부동소수점 의미론 중 하나가
붙습니다 — 따라서 호스트의 부동소수점 타입은 전혀 개입하지 않습니다.

## 레지스터

가상 레지스터는 저수준 타입과 배정으로 서술됩니다:

```c
NevercMIRVirtualRegisterDesc Desc = {0};
Desc.Header             = /* … */;
Desc.AssignmentKind     = NEVERC_MIR_REG_ASSIGNMENT_CLASS;
Desc.TargetID           = RegisterClassID;   /* 타깃 스키마가 필요 */
Desc.Type.Kind          = NEVERC_MIR_LLT_SCALAR;
Desc.Type.ScalarSizeInBits = 32;

uint32_t Register = 0;
MIR->CreateVirtualRegister(MIR->Context, Task, Mutation, &Desc, &Register);
```

배정 종류는 `NONE`, `GENERIC`, `CLASS`, `BANK` 이고, 저수준 타입 종류는
`INVALID`, `SCALAR`, `POINTER`, `VECTOR`, `POINTER_VECTOR` 이며, 확장 가능한
벡터에는 `IsScalable` 이 있습니다.

정의-사용 질의는 `GetRegisterDefCount` / `GetRegisterDef` 와
`GetRegisterUseCount` / `GetRegisterUse` 입니다. `ReplaceRegister` 는 한 번의
준비 작업으로 모든 출현을 고쳐 씁니다. 함수 수준 live-in 은 물리 레지스터와 그것이
복사되어 들어간 가상 레지스터를 짝지으며(`GetFunctionLiveIn`,
`AddFunctionLiveIn`, `RemoveFunctionLiveIn`), 블록 수준 live-in 은 레인 마스크를
함께 나릅니다(`AddBasicBlockLiveIn`, `RemoveBasicBlockLiveIn`).

## 스택 프레임

```c
int32_t FrameIndex = 0;
MIR->CreateStackObject(MIR->Context, Task, Mutation, /*Size=*/16,
                       /*Alignment=*/8, /*IsSpillSlot=*/NEVERC_FALSE,
                       /*StackID=*/0, &FrameIndex);
```

`CreateFixedStackObject` 는 알려진 오프셋에 객체를 놓고(`IsImmutable` 과
`IsAliased` 를 함께), `CreateVariableSizedStackObject` 는 동적 할당을 다룹니다.
나중에 `SetFrameObjectSize`, `SetFrameObjectAlignment`,
`SetFrameObjectOffset` 로 조정할 수 있습니다.

`NevercMIRFrameObjectInfo` 는 `Index`, `Flags`, `Size`, `Offset`, `Alignment`,
`StackID` 를 보고합니다. 프레임 플래그는 `FIXED`, `SPILL_SLOT`,
`VARIABLE_SIZED`, `IMMUTABLE`, `ALIASED`, `DEAD`, `PREALLOCATED` 입니다.
피호출자 보존 상태는 `GetCalleeSaved` 로 읽고 `SetCalleeSaved` 로 통째로
바꿉니다.

## 상수 풀, 점프 테이블, 메모리 오퍼랜드

상수 풀 항목은 값을 `NevercMIRWordView` 로 나르므로, 정수 항목과 부동소수점
항목이 같은 모양을 씁니다:

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

점프 테이블은 목적지 블록 배열에서 일곱 가지 항목 종류(`BLOCK_ADDRESS`,
`GP_REL64_BLOCK_ADDRESS`, `GP_REL32_BLOCK_ADDRESS`, `LABEL_DIFFERENCE32`,
`LABEL_DIFFERENCE64`, `INLINE`, `CUSTOM32`) 중 하나로 만듭니다.

메모리 오퍼랜드는 가장 풍부한 서술자입니다. 플래그(`LOAD`, `STORE`, `VOLATILE`,
`NON_TEMPORAL`, `DEREFERENCEABLE`, `INVARIANT`, 그리고 세 개의 타깃 플래그),
크기와 정렬, 아홉 종류 중 하나의 포인터(`IR_VALUE`, `FIXED_STACK`, `STACK`,
`CONSTANT_POOL`, `JUMP_TABLE`, `GOT`, `UNKNOWN_STACK`, `TARGET_CUSTOM`,
`UNKNOWN`), 성공과 실패 시의 원자적 순서, 동기화 범위, 그리고 TBAA,
alias-scope, no-alias, range 참조를 담습니다. 붙일 때는
`AddInstructionMemoryOperand` 를 씁니다.

## 트랜잭션 방식 변경

모든 변경은 하나의 머신 함수에 묶인 mutation 안에 준비됩니다:

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

커밋은 구조 사전 점검을 하고 이어서 Machine IR 검증기를 돌립니다. 잘못된
오퍼랜드, 망가진 CFG, 타깃 스키마가 실제 옵코드를 요구하는 자리에 쓰인 범용
옵코드, 지원되지 않는 속성 주장은 모두 원자적으로 되돌려집니다. 중단은 블록 순서,
명령어, 오퍼랜드, CFG 간선, 머신 속성을 원래 그대로 복원합니다.

`EndMutation` 은 핸들을 놓아주며 커밋·중단과는 별개입니다 — 두 경로 모두에서
호출하십시오.

준비할 수 있는 연산은 `CreateBasicBlock`, `MoveBasicBlock`, `EraseBasicBlock`,
`CreateInstruction`, `MoveInstruction`, `EraseInstruction`, `AppendOperand`,
`SetOperandValue`, `SetInstructionFlags`, `AddCFGEdge`, `RemoveCFGEdge`, 위에
나온 레지스터·프레임 호출, 상수 풀·점프 테이블 호출, 메모리 오퍼랜드 호출, 그리고
`SetMachinePropertyWithProof` 입니다.

## 머신 속성에는 증명이 필요하다

열한 가지 머신 속성 — `IS_SSA`, `NO_PH_IS`, `TRACKS_LIVENESS`, `NO_V_REGS`,
`FAILED_I_SEL`, `LEGALIZED`, `REG_BANK_SELECTED`, `SELECTED`,
`TIED_OPS_REWRITTEN`, `FAILS_VERIFICATION`, `TRACKS_DEBUG_USER_VALUES` — 은
자유롭게 읽히지만 결코 자유롭게 설정되지 않습니다:

```c
NevercMIRPropertyProof Proof = {0};
Proof.Header   = /* … */;
Proof.Property = NEVERC_MIR_PROPERTY_IS_SSA;
Proof.Kind     = NEVERC_MIR_PROPERTY_PROOF_INVALIDATION;
Proof.Value    = NEVERC_FALSE;
MIR->SetMachinePropertyWithProof(MIR->Context, Task, Mutation, &Proof);
```

증명은 두 종류입니다. `INVALIDATION` 은 여러분의 변경이 전제를 깨뜨린 속성을
지웁니다 — 보장을 포기하는 것은 안전하므로 이는 언제나 받아들여집니다.
`STRUCTURAL_CHECK` 는 속성을 세우기 전에 호스트에 검증을 요구하므로, `IS_SSA` 를
주장하려면 약속이 아니라 실제 검사라는 대가를 치러야 합니다.

## 패스

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

이것은 [`pluginsdk/examples/MachinePass.c`] 그대로입니다. 수준은 `MODULE`,
`FUNCTION`, `BASIC_BLOCK` 입니다. `RequiredAnalyses` 와 `PreservedAnalyses` 는
`NevercMIRBuiltinAnalysis` 의 배열이고, `RequiredTargetSchemaDigest` 는 그 패스가
자신을 위해 만들어지지 않은 스키마에서 실행되기를 거부하게 만듭니다.

호출은 `Task`, `Phase`, `PassID`, `Level`, 그 수준에서 유효한 `Function` 과
`BasicBlock`, `Core` 와 `Analyses` 테이블, 그리고 현재 활성인
`TargetSchemaDigest` 를 나릅니다.

보존은 `OutPreserved` 로 보고합니다 — `NEVERC_MIR_PRESERVE_NONE`, `_CFG`,
`_ALL`, 그리고 `Analyses` 의 명시적 목록. 커밋된 변경 뒤에 `PRESERVE_ALL` 을
주장하면 거부됩니다.

함수 패스는 병렬 코드 생성 파티션에서 실행될 수 있고, 모듈 수준 패스는 직렬화된
파이프라인 장벽에서 실행됩니다. 플러그인이 선언한 동시성·재진입 모델은 여전히
여러분 자신의 상태를 다스립니다.

## 분석

내장 여섯 가지: `LIVE_INTERVALS`, `LIVE_VARIABLES`, `SLOT_INDEXES`,
`DOMINATOR_TREE`, `LOOP_INFO`, `REGISTER_PRESSURE`.

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

이 밖에 `DominatorTreeDominates`, `GetLoopCount` / `GetLoopHeader` /
`GetLoopForBlock`, `GetSlotIndex`, `IsRegisterLiveInBlock`,
`GetRegisterPressureSetCount` / `GetRegisterPressure` 도 쓸 수 있습니다.

사용 가능 여부는 훅에 달려 있습니다. `post_isel` 에서 생존 구간을 요청하면 바탕이
되는 LLVM 분석이 아직 없으므로 `NEVERC_STATUS_CAPABILITY_UNAVAILABLE` 로
실패합니다. 커밋된 변경은 그것이 영향을 준 결과 핸들을 무효로 만듭니다.

## IR → MIR 하강 대체하기

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
/* … 머신 함수를 만든다 … */

NevercMIRModuleCoverageDescriptor Coverage = {0};
Coverage.Header              = /* … */;
Coverage.HandlesGlobals      = NEVERC_TRUE;
Coverage.HandlesConstructors = NEVERC_TRUE;
Coverage.HandlesDebugInfo    = NEVERC_FALSE;
Coverage.HandlesUnwind       = NEVERC_FALSE;
Provider->PublishMIRModule(Provider->Context, Frame, &Coverage, &Output);
```

커버리지 서술자는 부분적인 제공자가 정직함을 지키는 방법입니다. 실제로 하강시킨
것만 선언하면 나머지는 호스트가 직접 처리하며, 전역 변수·생성자·디버그 정보·되감기
표가 소리 없이 사라지는 일이 없습니다.

## 예제

```sh
cmake --build build-neverc --target neverc-plugin-example-machine-pass
build-neverc/bin/neverc \
  -fplugin=build-neverc/neverc/pluginsdk/examples/host/MachinePass.so \
  -O2 -fno-lto -c input.c -o input.o
```

CMake 가 여러분의 플랫폼에 맞게 만든 모듈 접미사를 쓰십시오.

## 규칙

- 콜백이 반환된 뒤에 태스크 핸들, MIR 핸들, 빌려온 뷰를 붙들고 있지 마십시오. 또
  핸들 값이나 LLVM 옵코드 번호를 지어내지 마십시오.
- `RequiresTargetSchema` 플래그가 설정된 값을 쓰기 전에 `GetSchemaDigest` 를
  컴파일 시에 넣어 둔 다이제스트와 견주십시오.
- 변경은 오직 mutation 안에서만 하십시오. 모든 `BeginMutation` 은 커밋이나 중단
  뒤에 정확히 하나의 `EndMutation` 에 이릅니다.
- 증명 없이 머신 속성을 주장하지 마십시오. 변경이 보장을 포기했다면
  `STRUCTURAL_CHECK` 보다 `INVALIDATION` 을 택하십시오.
- 커밋된 변경 뒤에 `NEVERC_MIR_PRESERVE_ALL` 을 절대 주장하지 마십시오.
- 필요한 분석이 여러분이 고른 훅에서 실제로 쓸 수 있는지 확인하십시오.
- 모든 테이블 헤더와 예약 필드를 초기화하십시오. 상태는 C 경계 너머로 반환하고,
  C++ 예외가 그것을 넘게 하지 마십시오.
- `neverc.mir.final_verify` 는 봉인되어 있습니다. 무슨 일이 있어도 실행됩니다.

규범적 선언, 스키마 상수, 단계 정책, 커버리지 증거는 [`PluginMIR.h`],
[`Schema/PluginMIRSchema.inc`], [`Schema/PhaseSchema.json`], [`coverage.json`] 을
참조하십시오.

<!-- reference links -->
[`coverage.json`]: coverage.json
[`PluginMIR.h`]: ../../neverc/include/neverc/Plugin/PluginMIR.h
[`pluginsdk/examples/MachinePass.c`]: ../../pluginsdk/examples/MachinePass.c
[`Schema/PhaseSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PhaseSchema.json
[`Schema/PluginMIRSchema.inc`]: ../../neverc/include/neverc/Plugin/Schema/PluginMIRSchema.inc
