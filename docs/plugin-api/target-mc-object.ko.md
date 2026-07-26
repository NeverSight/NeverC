**언어**: [English](target-mc-object.md) | [简体中文](target-mc-object.zh-CN.md) | [繁體中文](target-mc-object.zh-TW.md) | [日本語](target-mc-object.ja.md) | [한국어](target-mc-object.ko.md) | [Français](target-mc-object.fr.md) | [Deutsch](target-mc-object.de.md) | [Español](target-mc-object.es.md) | [Italiano](target-mc-object.it.md) | [Русский](target-mc-object.ru.md) | [العربية](target-mc-object.ar.md)

[← NeverC 플러그인 ABI](README.ko.md)

# NeverC 플러그인 타깃·MC·어셈블리·오브젝트 API

백엔드는 네 개의 헤더와 스물아홉 개의 페이즈입니다. [`PluginTarget.h`]는 타깃과 코
드 생성 경로를 기술합니다. [`PluginMC.h`]는 기계어를 만들고 관찰합니다. 어셈블리
파싱과 출력도 같은 헤더에 있습니다. [`PluginObject.h`]는 재배치 가능 파일을 정규화
된 그래프로 바꾸고 다시 되돌립니다.

이들을 합치면 플러그인은 타깃을 추가하고, 로워링 단계 하나 또는 전부를 교체하고,
명령어가 방출되는 순간마다 지켜보고, 어셈블리 방언을 정의하고, 오브젝트 파일을 재
작성할 수 있습니다——그것도 LLVM의 `MCInst`, `MCSection`, `object::ObjectFile`을
결코 노출하지 않는 순수 C ABI를 통해서입니다.

## 인터페이스

```c
#include "neverc/Plugin/PluginTarget.h"
#include "neverc/Plugin/PluginMC.h"
#include "neverc/Plugin/PluginObject.h"   /* includes both of the above */
```

| 인터페이스 | 테이블 | 슬롯 | 용도 |
|---|---|--:|---|
| `NEVERC_INTERFACE_TARGET_*` | `NevercTargetAPI` | 2 | `RegisterTarget`, `RegisterCodeGenEdge` |
| `NEVERC_INTERFACE_TARGET_ABI_*` | `NevercTargetABIAPI` | 1 | `RegisterABI` |
| `NEVERC_INTERFACE_CALLING_CONVENTION_*` | `NevercCallingConventionAPI` | 1 | `RegisterCallingConvention` |
| `NEVERC_INTERFACE_MC_*` | `NevercMCAPI` | 53 | `MCUnit` 읽기와 변경, 인코더·디코더·백엔드 등록 |
| `NEVERC_INTERFACE_MC_EMISSION_*` | `NevercMCEmissionAPI` | 7 | 방출 이벤트와 레이아웃 스냅숏 |
| `NEVERC_INTERFACE_MC_PROVIDER_*` | `NevercMCProviderAPI` | 4 | MIR → MC 교체 |
| `NEVERC_INTERFACE_ASSEMBLY_PROVIDER_*` | `NevercAssemblyProviderAPI` | 8 | 어셈블리 파서 또는 프린터 교체 |
| `NEVERC_INTERFACE_OBJECT_*` | `NevercObjectAPI` | 34 | ObjectGraph 읽기와 변경 |
| `NEVERC_INTERFACE_OBJECT_FORMAT_*` | `NevercObjectFormatAPI` | 1 | `RegisterFormat` |
| `NEVERC_INTERFACE_OBJECT_PHASE_*` | `NevercObjectPhaseAPI` | 2 | `GetGraph`, `GetImage` |

## 두 가지 호환성 등급

여기서 다루는 나머지 전부를 지배하는 규칙입니다.

**STABLE**, 하드코딩해도 안전한 것: 타깃 독립적인 디스크립터, 페이즈 ID, 아티팩트
ID, MC와 ObjectGraph 컨테이너, 출력 트랜잭션, 그리고 모든 콜백 계약.

**LOCKSTEP**, 확인 없이는 위험한 것: 타깃 고유의 opcode, 레지스터, 오퍼랜드,
fixup, 재배치, 호출 규약 스키마. 이들의 수치는 정확히 하나의 스키마 개정판에 대해
서만 의미를 가집니다.

LOCKSTEP 값이 나타나는 곳마다 그 옆에 스키마 다이제스트가 함께 있습니다. 값을 읽
기 전에 비교하세요:

```c
if (!string_equal(Target.SchemaDigest, MY_COMPILED_SCHEMA_DIGEST))
  return fail(NEVERC_STATUS_ABI_MISMATCH);
```

NeverC도 프로바이더를 호출하기 전에 어긋난 스키마를 거부하므로 이 검사는 이중 안
전장치입니다——하지만 이를 건너뛰고 원시 opcode를 읽는 플러그인은 명령어를 조용히
잘못 해석하게 됩니다.

## 페이즈들

스물아홉 개, 네 개의 도메인에 걸쳐 있습니다.

### `codegen` — 경로 선택 (4)

| 페이즈 | 정책 |
|---|---|
| `neverc.codegen.ir_to_mir` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE |
| `neverc.codegen.mir_to_mc` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE |
| `neverc.codegen.coarse_lower` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE |
| `neverc.codegen.product_verify` | OBSERVABLE, **SEALED** |

### `mc` — 기계어 (13)

`neverc.mc.encode`, `neverc.mc.decode`, `neverc.mc.layout`은 OBSERVABLE,
INTERCEPTABLE, REPLACEABLE입니다.

`neverc.mc.emission.pre_instruction`은 REPLACEABLE이기도 한 유일한 방출 이벤트로,
명령어를 대체하는 곳이 바로 여기입니다. 나머지 아홉 개(`unit_begin`, `unit_end`,
`section_change`, `post_instruction`, `post_encode`, `fixup`,
`relaxation_round`, `pre_layout`, `post_layout`)는 관찰 전용입니다.

### `assembly` (4)

`neverc.assembly.parse`와 `neverc.assembly.print`는 REPLACEABLE입니다.
`neverc.assembly.final_verify`와 `neverc.assembly.commit`은 SEALED입니다.

### `object` (8)

`neverc.object.probe`, `read`, `write`, `pre_write`, `post_layout`은
REPLACEABLE이고, `neverc.object.post_write`는 INTERCEPTABLE만 가능하며,
`neverc.object.final_verify`와 `neverc.object.commit`은 SEALED입니다.

## 타깃 등록

`NevercTargetDescriptor`는 이 ABI에서 가장 큰 디스크립터인데, 프런트엔드와 백엔드
가 알아야 할 모든 것을 담고 있기 때문입니다:

```c
typedef struct NevercTargetDescriptor {
  NevercABITableHeader Header;
  NevercTargetID TargetID;
  NevercStringView CanonicalName;
  NevercStringArrayView Aliases;
  NevercStructArrayView TripleMatchers;    /* NevercTargetTripleMatcher[] */
  NevercTargetABIID DefaultABI;
  NevercCallingConventionID DefaultCallingConvention;
  NevercInterfaceID MCSchemaID;
  NevercInterfaceID DefaultObjectFormatID;
  NevercTargetMachineDescriptor Machine;
  NevercStructArrayView Macros;            /* predefined macros           */
  NevercStructArrayView Builtins;          /* target builtins + lowering  */
  NevercStructArrayView Registers;         /* inline-asm register names   */
  NevercStructArrayView Constraints;       /* inline-asm constraints      */
  NevercStringView Clobbers;
  uint64_t Flags;
  NevercTargetValidateCPUFn ValidateCPU;
  NevercTargetCanonicalizeCPUFn CanonicalizeCPU;
  NevercTargetListCPUsFn ListCPUs;
  NevercTargetResolveFeaturesFn ResolveFeatures;
  NevercCreateTargetMachineFn CreateTargetMachine;
  NevercDestroyTargetMachineFn DestroyTargetMachine;
  void *UserData;
  NevercDestroyUserDataFn DestroyUserData;
} NevercTargetDescriptor;
```

`TripleMatchers`는 이 타깃이 언제 선택될지를 결정합니다. 각 매처는 아키텍처, 벤
더, 운영체제, 환경을 지정하고, 내장 타깃과 동점일 때 우열을 가르는 `Priority`도
함께 가집니다.

`Machine`은 `NevercTargetMachineDescriptor`입니다——데이터 레이아웃, 기본 및 튜닝
용 CPU, 기능 테이블, 지원 ABI·호출 규약·오브젝트 형식, 주소 공간, 재배치 모델과
코드 모델(기본값과 지원 마스크 양쪽), 예외 모델(`NONE`, `DWARF`, `SJLJ`, `SEH`,
`WASM`), 되감기 모델, 엔디언, pointer/int/long/long long의 너비, 스택 정렬, 원자
연산과 벡터의 최대 너비, `va_list` 종류, 실행 수준(`USER`, `KERNEL`,
`HYPERVISOR`, `FIRMWARE`), 그리고 TLS 지원.

타깃 내장 함수는 저마다 로워링 콜백을 지니며, 그 콜백은 살아 있는 IR 빌더를 받습
니다:

```c
static NevercStatus NEVERC_CALL
lower_builtin(void *UserData,
              const NevercTargetBuiltinLoweringInvocation *In,
              NevercIRValueHandle *OutResult) {
  /* In->Core, In->Builder, In->Mutation, In->IRBuilder,
     In->ResultType, In->Arguments, In->ArgumentCount */
  return In->Builder->BuildCall(/* … */);
}
```

## ABI와 호출 규약

ABI는 함수 시그니처를 분류합니다:

```c
static NevercStatus NEVERC_CALL
classify(void *UserData, const NevercABIFunctionQuery *Query,
         NevercABIArgumentClassification *ReturnValue,
         NevercABIArgumentClassificationArray *Arguments) {
  ReturnValue->Kind = NEVERC_ABI_ARGUMENT_DIRECT;
  for (uint64_t I = 0; I != Arguments->Count; ++I) {
    NevercABIArgumentClassification *A = &Arguments->Data[I];
    A->Kind  = NEVERC_ABI_ARGUMENT_INDIRECT;
    A->Flags = NEVERC_ABI_ARGUMENT_BYVAL;
  }
  return neverc_status_ok();
}
```

인자 종류는 `DIRECT`, `EXTEND`, `INDIRECT`, `IGNORE`, `EXPAND`,
`INDIRECT_ALIASED`, `COERCE_AND_EXPAND`이고, 플래그는 `BYVAL`, `REALIGN`,
`INREG`, `SRET_AFTER_THIS`, `CAN_BE_FLATTENED`, `SIGN_EXTEND`, `PADDING_INREG`입
니다. 강제 변환은 `NONE`, `INTEGER`, `FLOAT`, `POINTER` 중 하나이며,
`COERCE_AND_EXPAND`는 `NevercABICoercionElement` 배열을 제공합니다.

호출 규약은 한 단계 더 내려가 실제 위치를 배정합니다:

```c
static NevercStatus NEVERC_CALL
plan(void *UserData, const NevercCallingConventionQuery *Query,
     NevercCallingConventionPlan *Plan) {
  /* Query->TargetID, ->CallingConventionID, ->SchemaDigest, ->Function */
  /* Fill Plan->ReturnLocations and Plan->ArgumentLocations with
     NevercCallingConventionLocation records: REGISTER or STACK,
     ValueIndex, PieceOffset, Size, Alignment, RegisterNumber,
     StackOffset, and INDIRECT / BYVAL flags.                       */
  Plan->CalleeSavedRegisters = MySavedRegisters;
  Plan->StackAlignment       = 16;
  return neverc_status_ok();
}
```

`Query->SchemaDigest`는 LOCKSTEP 값입니다——`RegisterNumber`는 그것이 지목하는 스
키마에 대해서만 의미를 가집니다. 완전한 실작업 예시는
[사용자 정의 호출 규약](custom-callconv/README.ko.md#구체화된-plan)과
[`pluginsdk/examples/CustomCallConvPlugin.c`]를 보세요.

## 코드 생성 경로

경로는 정규 `NevercTargetKey`에서 선택됩니다: 타깃 ID, 트리플 각 부분, CPU, 튜닝
CPU, 기능, ABI, 호출 규약, 오브젝트 형식, 재배치 모델, 코드 모델, 실행 수준, 포인
터 너비, 엔디언, 스키마 다이제스트. 여러분이 담당할 수 있는 간선을 등록하세요:

```c
NevercCodeGenEdgeDescriptor Edge = {0};
Edge.Header          = /* … */;
Edge.EdgeID          = MyEdgeID;
Edge.CanonicalName   = SV("com.example.mir-to-mc");
Edge.TargetID        = MyTargetID;
Edge.InputKind       = NEVERC_CODEGEN_PRODUCT_MIR;
Edge.OutputKind      = NEVERC_CODEGEN_PRODUCT_MC;
Edge.CompatibilityKey = SV("…");
Edge.ProviderID      = SV("com.example.backend");
Target->RegisterCodeGenEdge(Target->Context, RegistrarContext, &Edge);
```

산출물 종류는 `IR`, `MIR`, `MC`, `ASSEMBLY`, `OBJECT_GRAPH`, `OBJECT_IMAGE`,
`CUSTOM`입니다. 세분화된 경로는 `IR → MIR → MC → ObjectGraph → ObjectImage`입니
다.

`NEVERC_CODEGEN_EDGE_COARSE`를 설정하고 `CoarseLower`를 제공하면
`IR → ObjectImage` 구간 전체를 한 번에 대체합니다:

```c
static NevercStatus NEVERC_CALL
coarse_lower(void *UserData, NevercTaskHandle Task,
             const NevercCodeGenRequest *Request,
             NevercCodeGenProductCandidate *OutCandidate) {
  /* Request->Target, ->Input, ->InputKind, ->OutputKind,
     ->OptimizationLevel, ->HasFinalIRProof                */
  OutCandidate->Kind      = NEVERC_CODEGEN_PRODUCT_OBJECT_IMAGE;
  OutCandidate->Artifact  = MyImage;
  OutCandidate->ProductID = MyProductID;
  return neverc_status_ok();
}
```

거친 경로라도 `neverc.codegen.product_verify`와 트랜잭션 기반 출력 커밋은 그대로
거칩니다. `VerifyProduct`는 호스트가 여러분이 이행했으리라 기대하는 의무들
——`VERIFY_FINAL_IR`, `VERIFY_TARGET_KEY`, `VERIFY_PRODUCT_KIND`,
`VERIFY_PRODUCT_ID`, `VERIFY_STRUCTURE`——과 함께 호출되므로, 프로바이더가 지름길
을 택해 게이트를 슬쩍 건너뛸 수는 없습니다.

## MC 구성하기

`MCUnit`은 섹션, 심볼, 표현식, 프래그먼트, 명령어, 오퍼랜드, fixup을 담습니다. 읽
기는 first/next 순회입니다:

```c
NevercMCUnitInfo Unit = {0};
Unit.Header = /* … */;
MC->GetUnitInfo(MC->Context, Task, UnitHandle, &Unit);

NevercMCSectionHandle Section;
MC->GetFirstSection(MC->Context, Task, UnitHandle, &Section);
while (!neverc_handle_is_null(Section)) {
  NevercMCFragmentHandle Fragment;
  MC->GetFirstFragment(MC->Context, Task, Section, &Fragment);
  /* … */
  MC->GetNextSection(MC->Context, Task, Section, &Section);
}
```

변경은 다른 곳과 마찬가지로 트랜잭션 기반입니다:

```c
NevercMCMutationHandle Mutation;
MC->BeginMutation(MC->Context, Task, Unit, &Mutation);
MC->CreateSection(MC->Context, Task, Mutation, &SectionDescriptor, &Section);
MC->CreateSymbol(MC->Context, Task, Mutation, &SymbolDescriptor, &Symbol);
MC->AppendInstruction(MC->Context, Task, Mutation, Section, &Instruction);
Status = MC->CommitMutation(MC->Context, Task, Mutation);
if (Status.Code != NEVERC_STATUS_OK)
  MC->AbandonMutation(MC->Context, Task, Mutation);
```

핸들은 태스크 스코프이며 세대 검사를 거치므로, 포기된 변경에서 나온 핸들은 재사용
되는 대신 거부됩니다.

섹션 플래그는 `ALLOCATED`, `EXECUTABLE`, `WRITABLE`, `MERGEABLE`, `DEBUG`입니다.
심볼 바인딩은 `LOCAL`, `GLOBAL`, `WEAK`, 타입은 `NONE`, `FUNCTION`, `OBJECT`,
`SECTION`, `TLS`, 정의는 `UNDEFINED`, `SECTION`, `ABSOLUTE`, `COMMON`입니다. 표현
식은 단항 `PLUS`, `MINUS`, `NOT`과 이항 `ADD`, `SUBTRACT`, `MULTIPLY`, `DIVIDE`,
`AND`, `OR`, `XOR`, `SHIFT_LEFT`, `SHIFT_RIGHT`를 지원합니다. 호스트가 대신 배치
해 주기를 바라는 곳에는 `NEVERC_MC_AUTOMATIC_OFFSET`을 넘기세요.

`RegisterSchema`는 타깃 MC 스키마를 게시하고, `GetSchemaToken` /
`GetSchemaTokenInfo`는 이름과 LOCKSTEP 토큰을 서로 변환합니다.

## 방출 관찰하기

방출 스트림은 열한 가지 이벤트 종류를 순서대로 보고합니다. 옵저버로 구독하고 이벤
트를 읽으세요:

```c
NevercMCEmissionEventInfo Event = {0};
Event.Header = /* … */;
Emission->GetEvent(Emission->Context, Frame, &Event);
/* Event.Kind, Event.Flags */
```

`Flags`는 이벤트의 어느 부분이 채워졌는지 알려 줍니다: `HAS_SECTION`,
`HAS_INSTRUCTION`, `HAS_ENCODING`, `HAS_FIXUP`, `HAS_LAYOUT`,
`CAN_REPLACE_INSTRUCTION`. 해당 필드를 읽기 전에 플래그를 확인하세요——아직 인코딩
이 없는 이벤트는 여러분이 물어봤다고 해서 생기지 않습니다.

`HAS_LAYOUT`이 설정되면 `GetLayoutSection`, `GetLayoutFragment`,
`GetLayoutSymbol`, `GetLayoutFixup`이 주소와 크기를 알려 줍니다.

`pre_instruction`에서, 그리고 `CAN_REPLACE_INSTRUCTION`이 설정된 경우에만 대체할
수 있습니다:

```c
Emission->BeginInstructionReplacement(Emission->Context, Frame, &Builder);
/* build the replacement through the MC builder */
Emission->PublishInstructionReplacement(Emission->Context, Frame, NewInstr);
```

[`pluginsdk/examples/MCObserverPlugin.c`]가 이것의 읽기 전용 버전입니다.

## 인코더, 디코더, 레이아웃

세 가지 등록이 기계어 백엔드를 확장하며, 모두 타깃과 스키마 다이제스트를 키로 삼
습니다:

```c
MC->RegisterEncoder(MC->Context, RegistrarContext, &EncoderDescriptor);
MC->RegisterDecoder(MC->Context, RegistrarContext, &DecoderDescriptor);
MC->RegisterAsmBackend(MC->Context, RegistrarContext, &BackendDescriptor);
```

인코더는 버퍼를 반환하는 대신 싱크를 통해 씁니다. 그래야 소유권이 호스트 쪽에 남
습니다:

```c
Sink->WriteBytes(Sink->Context, Bytes);
Sink->AddFixup(Sink->Context, &Fixup);
```

디코더는 `NEVERC_MC_DECODE_SUCCESS`, `_SOFT_FAIL`, `_UNKNOWN`, `_FAIL` 중 하나를
보고합니다. fixup 종류는 `NevercMCFixupKindInfo`를 통해 `PC_RELATIVE`, `SIGNED`,
`RELAXABLE`, `TARGET` 플래그로 스스로를 설명합니다.

asm 백엔드가 완화(relaxation)를 담당합니다. 레이아웃은 증명 다이제스트를 내놓으
며, **레이아웃 이후의 어떤 변경이든 그 증명을 무효화**하고 오브젝트를 쓰기 전에 재
레이아웃을 강제합니다——링크 그래프가 쓰는 것과 같은 세대 검사 방식입니다.

## 어셈블리

파서 프로바이더는 소스 바이트를 소비해 `MCUnit`을 게시합니다:

```c
NevercAssemblyParseInputInfo In = {0};
In.Header = /* … */;
Asm->GetParseInput(Asm->Context, Frame, &In);

NevercAssemblyTokenInfo Token = {0};
Asm->PeekSourceToken(Asm->Context, Frame, &Token);
Asm->AdvanceSourceToken(Asm->Context, Frame);

const NevercMCAPI *MC;
NevercMCUnitHandle Unit;
Asm->GetParseMCBuilder(Asm->Context, Frame, &MC, &Unit);
/* … build … */
Asm->PublishParsedMCUnit(Asm->Context, Frame, Unit, &Output);
```

소스는 `NEVERC_ASSEMBLY_SOURCE_BUFFER` 아니면
`NEVERC_ASSEMBLY_SOURCE_RENDERED_TOKENS`입니다. 전처리된 어셈블리(`.S`)는 먼저 일
반 프런트엔드 전처리기를 거쳐 렌더링된 토큰으로 도착하고, 순수 어셈블리(`.s`)는
버퍼로서 파서에 바로 들어갑니다.

프린터는 반대 방향입니다——`GetPrintInput` 다음에 제공된 출력 트랜잭션으로
`WritePrintOutput`, 그다음 `PublishAssemblyOutput`. 그 밖의 곳에 쓰는 것은 지원되
지 않습니다. 파싱/출력 검증과 호스트 커밋 게이트가 바이트가 보이기 전에 실행되므
로, 출력에 실패해도 잘린 파일이 남지 않습니다.

## 오브젝트 그래프

`NevercObjectAPI`는 재배치 가능 파일을 섹션, 심볼, 재배치, COMDAT으로 정규화합니
다. 내장 어댑터가 ELF, COFF, Mach-O를 다루며, `RegisterFormat`으로 더 추가할 수
있습니다.

```c
NevercObjectGraphInfo Info = {0};
Info.Header = /* … */;
Object->GetGraphInfo(Object->Context, Task, Graph, &Info);
/* Info.Target, .ObjectSchemaDigest, .Generation, .SectionCount,
   .SymbolCount, .RelocationCount, .ComdatCount, .HasLayoutProof */

NevercObjectSymbolHandle Symbol;
Object->GetFirstSymbol(Object->Context, Task, Graph, &Symbol);
while (!neverc_handle_is_null(Symbol)) {
  NevercObjectSymbolInfo SymInfo = {0};
  SymInfo.Header = /* … */;
  Object->GetSymbolInfo(Object->Context, Task, Symbol, &SymInfo);
  Object->GetNextSymbol(Object->Context, Task, Symbol, &Symbol);
}
```

변경은 네 가지 엔티티 종류 모두에 대해 create/replace/move/erase 패턴을 따르며,
`BeginMutation` … `CommitMutation` / `AbandonMutation` 안에서 스테이징됩니다.

섹션 플래그는 `ALLOCATED`, `EXECUTABLE`, `WRITABLE`, `MERGEABLE`, `STRINGS`,
`TLS`, `DEBUG`, `UNWIND`, `DISCARDABLE`, `RETAIN`입니다. 재배치 대상은 `SYMBOL`,
`SECTION`, `ABSOLUTE`, `FORMAT_EXTENSION` 중 하나입니다.

모든 디스크립터에는 `ExtensionOwner` / `ExtensionVersion` / `Extension` 삼총사가
있습니다. 정규화 그래프에 마땅한 필드가 없는 데이터를 형식이 보존하는 방법이 바로
이것입니다——그 바이트들은 엔티티와 함께 이동했다가 쓰기 시점에 되돌아오므로, 왕복
과정에서 버려지지 않습니다.

### 형식 등록하기

```c
NevercObjectFormatDescriptor Format = {0};
Format.Header           = /* … */;
Format.FormatID         = MyFormatID;
Format.CanonicalName    = SV("com.example.myfmt");
Format.SupportedTargets = MyTargets;
Format.DefaultExtension = SV(".mof");
Format.Flags            = NEVERC_OBJECT_FORMAT_CAN_PROBE |
                          NEVERC_OBJECT_FORMAT_CAN_READ  |
                          NEVERC_OBJECT_FORMAT_CAN_WRITE;
Format.Probe            = probe;
Format.Reader           = read;
Format.Writer           = write;
ObjectFormat->RegisterFormat(ObjectFormat->Context, RegistrarContext,
                             &Format);
```

`Probe`는 0에서 `NEVERC_OBJECT_PROBE_MAX_CONFIDENCE`(1000) 사이의
`Confidence`, 자신이 인식한 `NevercObjectArtifactKind`(`RELOCATABLE`, `ARCHIVE`,
`EXECUTABLE_IMAGE`, `SHARED_IMAGE`, `UNIVERSAL_BINARY`), 그리고 확신하기까지 필요
했던 바이트 수인 `ConsumedMinimum`(상한은
`NEVERC_OBJECT_PROBE_MAX_CONSUMED_MINIMUM`, 65536)을 보고합니다. 확신도가 가장 높
은 쪽이 이깁니다.

`Reader`에는 그래프와 열린 변경이 전달되어 그것을 채웁니다. `Writer`에는 그래프,
그 레이아웃 증명, 그리고 경계가 있는 바이너리 빌더가 전달됩니다.

### 쓰기 파이프라인

1. 탐지해서 바이트를 ObjectGraph로 읽어 들인다;
2. `object.pre_write` 그래프 인터셉터를 실행한다;
3. 레이아웃한 다음 `object.post_layout`을 실행한다(변경 후에는 재레이아웃);
4. 경계가 있는 후보 이미지를 쓴다;
5. `object.post_write` 바이너리 인터셉터를 실행한다;
6. 봉인된 `object.final_verify`와 원자적 `object.commit`을 실행한다.

이미지 상태는 `CANDIDATE` → `VERIFIED` → `COMMITTED`로, 또는 `ABORTED` /
`FAILED_PARTIAL`로 이동합니다.

옵저버는 읽기 전용 브리지를 받으며, 옵저버에서 변경을 시도하면
`NEVERC_STATUS_POLICY_VIOLATION`으로 거부됩니다. 라이터와 post-write 인터셉터는
경계가 있는 `NevercMutableBinaryAPI` 빌더만 받습니다——`Reserve`, `Write`,
`WriteAt`, `Tell`, `ReadAt`, `Insert`, `Append`, `Resize`. 오버플로, 콜백 실패,
검증 실패는 스테이징을 중단시키므로, 실패가 디스크에 반쪽짜리 파일을 남기는 일은
없습니다.

[`pluginsdk/examples/ObjectRewritePlugin.c`]가 완전한 트랜잭션 기반 재작성 예제입니
다.

## 규칙

- LOCKSTEP인 opcode, 레지스터, 오퍼랜드, fixup, 재배치, 호출 규약 값을 사용하기
  전에 스키마 다이제스트를 비교한다.
- 가변 상태는 호스트가 제공하는 process, session, task 상태에 둔다.
- 콜백이 반환된 뒤에는 태스크 핸들이나 빌린 뷰를 캐시하지 않는다.
- 인터셉터의 연속은 콜백 스레드에서 많아야 한 번만 호출한다.
- 모든 `BeginMutation`은 정확히 한 번의 커밋 또는 포기에 도달한다.
- 레이아웃이 끝난 MCUnit이나 ObjectGraph를 변경했다면 재레이아웃한다. 예전 레이아
  웃 증명은 낡았고 호스트가 거부한다.
- 이벤트 필드를 읽기 전에 `NevercMCEmissionEventInfo.Flags`를 확인하고, 명령어 대
  체는 `CAN_REPLACE_INSTRUCTION`이 설정된 경우에만 한다.
- 출력은 제공된 트랜잭션이나 바이트 싱크를 통해서만 쓴다.
- 실패 시에는 원래의 `NevercStatus`를 반환하고 반쪽짜리는 아무것도 게시하지 않는
  다.
- 사실에 부합하는 가장 좁은 동시성·재진입 모델을 선언한다.
- `codegen.product_verify`, `assembly.final_verify`, `assembly.commit`,
  `object.final_verify`, `object.commit`은 봉인되어 있다. 관찰만 하라.

규범적 선언은 [`PluginTarget.h`], [`PluginMC.h`], [`PluginObject.h`],
[`Schema/PhaseSchema.json`]을 보세요. 이들이 쓰는 엔티티·오퍼랜드·fixup·섹션 종류는
[`Schema/MCSchema.json`]과 [`Schema/ObjectSchema.json`]에서 오며, 각각
[`Schema/PluginMCSchema.inc`]와 [`Schema/PluginObjectSchema.inc`]를 생성합니다. 이
안정 페이즈들을 각각 긍정·부정·교체·읽기 전용 옵저버·봉인 게이트 테스트에 매핑한
것은 [`coverage.json`]을 보세요.

<!-- reference links -->
[`coverage.json`]: coverage.json
[`PluginMC.h`]: ../../neverc/include/neverc/Plugin/PluginMC.h
[`PluginObject.h`]: ../../neverc/include/neverc/Plugin/PluginObject.h
[`pluginsdk/examples/CustomCallConvPlugin.c`]: ../../pluginsdk/examples/CustomCallConvPlugin.c
[`pluginsdk/examples/MCObserverPlugin.c`]: ../../pluginsdk/examples/MCObserverPlugin.c
[`pluginsdk/examples/ObjectRewritePlugin.c`]: ../../pluginsdk/examples/ObjectRewritePlugin.c
[`PluginTarget.h`]: ../../neverc/include/neverc/Plugin/PluginTarget.h
[`Schema/MCSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/MCSchema.json
[`Schema/ObjectSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/ObjectSchema.json
[`Schema/PhaseSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PhaseSchema.json
[`Schema/PluginMCSchema.inc`]: ../../neverc/include/neverc/Plugin/Schema/PluginMCSchema.inc
[`Schema/PluginObjectSchema.inc`]: ../../neverc/include/neverc/Plugin/Schema/PluginObjectSchema.inc
