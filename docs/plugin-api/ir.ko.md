**언어**: [English](ir.md) | [简体中文](ir.zh-CN.md) | [繁體中文](ir.zh-TW.md) | [日本語](ir.ja.md) | [한국어](ir.ko.md) | [Français](ir.fr.md) | [Deutsch](ir.de.md) | [Español](ir.es.md) | [Italiano](ir.it.md) | [Русский](ir.ru.md) | [العربية](ir.ar.md)

# NeverC 플러그인 IR API

`PluginIR.h` 는 여섯 개의 능력 테이블과 생성된 스키마를 통해 LLVM IR 을
공개합니다. 플러그인은 IR 을 읽고 다시 쓰고, 다섯 개의 안정된 파이프라인 지점에
패스를 등록하고, 자체 분석을 정의하고, 심지어 IR 생성과 최적화 파이프라인을
통째로 대체할 수도 있습니다 — LLVM 헤더를 단 하나도 포함하지 않고서 말입니다.

옵코드, 타입 종류, 명령어 속성은 LLVM 열거값이 아니라 **안정된 스키마 ID** 입니다.
바로 그 간접성 덕분에 오늘 컴파일한 플러그인이 호스트가 새 LLVM 릴리스로 옮겨간
뒤에도 계속 동작합니다.

## 인터페이스

```c
#include "neverc/Plugin/PluginIR.h"
```

| 인터페이스 | 테이블 | 슬롯 | 목적 |
|---|---|--:|---|
| `NEVERC_INTERFACE_IR_CORE_{HIGH,LOW}` | `NevercIRCoreAPI` | 99 | 모듈, 값, 타입, 상수, 메타데이터, 속성 읽기/편집 |
| `NEVERC_INTERFACE_IR_BUILDER_{HIGH,LOW}` | `NevercIRBuilderAPI` | 29 | 트랜잭션 방식 구성 |
| `NEVERC_INTERFACE_IR_ANALYSIS_{HIGH,LOW}` | `NevercIRAnalysisAPI` | 13 | 내장 분석과 플러그인 분석 |
| `NEVERC_INTERFACE_IR_PASS_{HIGH,LOW}` | `NevercIRPassAPI` | 1 | `RegisterPass` |
| `NEVERC_INTERFACE_IR_GEN_{HIGH,LOW}` | `NevercIRGenAPI` | 5 | SemanticUnit → IR 하강을 대체 |
| `NEVERC_INTERFACE_IR_OPTIMIZATION_{HIGH,LOW}` | `NevercIROptimizationAPI` | 7 | 최적화 파이프라인 전체를 대체 |

모두 major 1 에서 `NEVERC_INTERFACE_STABLE` 입니다. 대응하는
`NEVERC_IR_*_API_MAJOR` / `_MINOR` 로 협상하고, `TableSize` 가 여러분이 호출할
마지막 슬롯까지 닿는지를 `pluginsdk/examples/FunctionPass.c` 처럼 확인하십시오:

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

## 단계

여덟 개의 IR 단계가 있습니다:

| 단계 | 정책 |
|---|---|
| `neverc.ir.generate` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE |
| `neverc.ir.optimize` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE |
| `neverc.ir.pass.pre_opt` | OBSERVABLE, INTERCEPTABLE |
| `neverc.ir.pass.pipeline_start` | OBSERVABLE, INTERCEPTABLE |
| `neverc.ir.pass.optimizer_last` | OBSERVABLE, INTERCEPTABLE |
| `neverc.ir.pass.post_opt` | OBSERVABLE, INTERCEPTABLE |
| `neverc.ir.pass.pre_codegen` | OBSERVABLE, INTERCEPTABLE |
| `neverc.ir.final_verify` | OBSERVABLE, **봉인된 호스트 관문** |

다섯 개의 `pass.*` 단계가 `NevercIRPassDescriptor.Phase` 가 가리키는 곳입니다.
`neverc.ir.final_verify` 는 LLVM 검증기를 실행하며, 최적화 제공자를 포함해 그
무엇도 이를 가로채거나 대체하거나 건너뛸 수 없습니다.

## 스키마

`Schema/PluginIRSchema.inc` 는 생성물이며 `PluginIR.h` 가 포함합니다. 다이제스트와
다음 상수 집합을 공개합니다:

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

ID 는 상위 바이트로 영역이 표시됩니다 — 타입은 `0x41……`, 값 종류는 `0x42……`,
옵코드는 `0x43……`, 속성은 `0x49……` — 따라서 엉뚱한 자리에 쓰인 값은 잘못
읽히는 대신 거부됩니다.

## 핸들과 소유권

IR 핸들은 하나의 태스크에 한정된 불투명한 `{Owner, Value}` 쌍이며, 그 뒤에 있는
모든 것은 호스트가 소유합니다.

- 콜백이나 태스크가 끝난 뒤에 핸들을 붙들고 있지 마십시오.
- 다른 세션이나 태스크에서 핸들을 쓰지 마십시오.
- 커밋된 교체는 교체된 객체의 핸들을 무효로 만듭니다.
- 중단된 변경은 그 변경이 만든 핸들을 낡은 것으로 만듭니다.
- 오류는 `NEVERC_STATUS_STALE_HANDLE`, `WRONG_SCOPE`, `WRONG_TYPE` 이며, 결코
  날것의 LLVM 포인터가 아닙니다.

질의에서 나온 문자열과 바이트 뷰는 콜백 동안만 빌려온 것입니다. 유일한 예외는
`ExportModule` 로, 이는 `NevercIRSerializedBufferHandle` 을 돌려주며 반드시
`ReleaseSerializedBuffer` 에 되돌려주어야 합니다.

## 모듈 훑기

컬렉션은 자기 세대 번호를 지닌 커서로 읽습니다. 그래서 훑는 도중에 변경이
일어나면 조용히 항목을 건너뛰는 대신 감지됩니다:

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

`Count` 가 0 으로 돌아올 때까지 반복하십시오. 일곱 개의 컬렉션은
`MODULE_FUNCTIONS`, `MODULE_GLOBALS`, `MODULE_ALIASES`, `MODULE_I_FUNCS`,
`FUNCTION_ARGUMENTS`, `FUNCTION_BLOCKS`, `BLOCK_INSTRUCTIONS` 입니다.

그 밖의 모든 것은 직접 질의입니다: `GetValueKind`, `GetValueType`,
`GetOperandCount` / `GetOperand` / `SetOperand`, `GetValueUseCount` /
`GetValueUse`, `GetTerminator`, `GetPredecessor*`, `GetSuccessor*`,
`GetPHIIncoming*`, 그리고 모듈 수준의 `GetModuleIdentifier`,
`GetModuleTargetTriple`, `GetModuleDataLayout`, `GetModuleInlineAssembly` 와 그
설정자들.

## 타입과 상수

타입은 인터닝되어 있으므로 두 번 물어도 같은 핸들이 나옵니다:

```c
NevercIRTypeHandle I32, Ptr, Fn;
Core->GetIntegerType(Core->Context, Task, 32, &I32);
Core->GetPointerType(Core->Context, Task, /*AddressSpace=*/0, &Ptr);

NevercIRTypeHandle Params[] = {I32, Ptr};
Core->GetFunctionType(Core->Context, Task, I32, Params, 2,
                      /*Variadic=*/0, &Fn);
```

`GetPrimitiveType` 은 `NEVERC_IR_TYPE_VOID`, `_FLOAT`, `_DOUBLE`, `_TOKEN`
같은 스키마 종류를 받습니다. 나머지는 `GetArrayType`, `GetVectorType`(`Scalable`
플래그 포함), `GetStructType`(이름 있는 것이든 리터럴이든, packed 이든 아니든)이
담당합니다.

정수와 부동소수점 상수는 리틀엔디언 64비트 워드로 만들어지므로 `i128` 에도 특별한
경로가 필요 없습니다:

```c
uint64_t Words[2] = {0xFFFFFFFFFFFFFFFFULL, 0x1ULL};
NevercIRValueHandle C;
Core->CreateIntegerConstant(Core->Context, Task, I128, Words, 2, &C);
```

`GetNullConstant`, `GetPoisonConstant`, `GetUndefConstant`,
`CreateAggregateConstant`, `GetGlobalAddressConstant` 가 단순한 경우를 덮고,
`CreateConstantBinaryExpression`, `CreateConstantCastExpression`,
`CreateConstantCompareExpression`, `CreateConstantGEPExpression` 이 상수식을
만듭니다.

## 명령어 속성

플래그마다 접근자를 두는 대신, 명령어의 세부 사항은 스키마 ID 를 키로 하는 태그가
붙은 속성 값을 거칩니다:

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

23가지 속성은 `NAME`, `FAST_MATH_FLAGS`, `NUW`, `NSW`, `EXACT`, `DISJOINT`,
`VOLATILE`, `ALIGNMENT`, `ATOMIC_ORDERING`, `SYNC_SCOPE`, `PREDICATE`,
`CALLING_CONVENTION`, `TAIL_CALL_KIND`, `INDICES`, `WEAK`,
`SUCCESS_ORDERING`, `FAILURE_ORDERING`, `INBOUNDS`, `SOURCE_ELEMENT_TYPE`,
`ALLOCATED_TYPE`, `ATTRIBUTES`, `CLEANUP`, `NUSW` 입니다. 원자적 순서는
`NOT_ATOMIC` 부터 `SEQUENTIALLY_CONSISTENT` 까지, tail-call 종류는 `NONE`,
`TAIL`, `MUST_TAIL`, `NO_TAIL`, fast-math 플래그는 `ALLOW_REASSOC` 부터
`APPROX_FUNC` 까지 익숙한 일곱 비트입니다.

## 속성(Attribute)

속성은 먼저 만들고 나서 붙이는 값이며, 그 덕분에 네 종류(`ENUM`, `INTEGER`,
`STRING`, `TYPE`)를 한결같이 다룰 수 있습니다:

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

`pluginsdk/examples/CustomCallConvPlugin.c` 는 이것을
`GetFunctionStringAttribute` 와 함께 써서 데이터로 정의된 호출 규약을 구동합니다.

## 트랜잭션 방식 변경

구조적 변경은 `NevercIRBuilderAPI` 를 거칩니다. 변경(mutation)이 트랜잭션이고,
빌더는 그 안의 커서입니다.

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

범위는 `NEVERC_IR_MUTATION_SCOPE_MODULE`, `_FUNCTION`, `_LOOP` 이고,
`ScopeRoot` 가 대상 함수나 루프 헤더를 지목합니다. 커밋은 후보를 검증하고
원자적으로 공개합니다 — 검증기가 실패하면 호스트는 되돌리고, 이전 모듈은 손대지
않은 채 남습니다.

빌드 호출은 `BuildBinary`, `BuildUnary`, `BuildCompare`, `BuildCast`,
`BuildSelect`, `BuildAlloca`, `BuildLoad`, `BuildStore`, `BuildGetElementPtr`,
`BuildCall`, `BuildPhi`, `BuildBranch`, `BuildConditionalBranch`,
`BuildUnreachable`, `BuildReturn`, `BuildReturnVoid` 입니다.
`SetDebugLocation` 과 `SetFastMathFlags` 는 그 뒤로 빌더가 내보내는 모든 것에
적용됩니다.

이 비대칭에 주의하십시오. `AddPhiIncoming`, `CreateFunction`,
`CreateBasicBlock` 은 빌더가 아니라 **mutation** 을 받습니다. 삽입 지점에 매이지
않기 때문입니다.

`DestroyMutation` 은 커밋이나 중단과 별개입니다. 모든 `BeginMutation` 에는 정확히
하나의 `DestroyMutation` 이 필요하며, 트랜잭션이 어떻게 끝났든 마찬가지입니다.

## 패스

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

수준은 `MODULE`, `CGSCC`, `FUNCTION`, `LOOP` 입니다. 호출에는 그 수준에서 유효한
핸들만 실립니다:

```c
typedef struct NevercIRPassInvocation {
  NevercABITableHeader Header;
  NevercTaskHandle Task;
  NevercInterfaceID Phase;
  NevercStringView PassID;
  NevercIRPassLevel Level;
  NevercIROptimizationLevel OptimizationLevel;  /* O0…O3, Os, Oz */
  NevercIRModuleHandle Module;
  NevercIRValueHandle Function;                 /* FUNCTION 과 LOOP      */
  NevercIRValueHandle LoopHeader;               /* LOOP 전용             */
  const NevercIRValueHandle *SCCFunctions;      /* CGSCC 전용            */
  uint64_t SCCFunctionCount;
  const NevercIRCoreAPI *Core;
  const NevercIRBuilderAPI *Builder;
  const NevercIRAnalysisAPI *Analyses;
  uint64_t Reserved[2];
} NevercIRPassInvocation;
```

세 개의 API 포인터가 호출과 함께 오므로, 패스 본문은 테이블을 따로 보관할 필요가
없습니다.

무엇이 살아남았는지는 `OutPreserved` 로 보고합니다:

```c
OutPreserved->Flags = NEVERC_IR_PRESERVE_ALL;   /* 또는 _NONE, _CFG */
```

`NEVERC_IR_PRESERVE_CFG` 는 명령어가 바뀌었어도 제어 흐름 그래프는 온전하다는
뜻입니다. 사용자 정의 분석은 `CustomAnalyses` 에 나열해야 보존됩니다. IR 을 바꾼
뒤에 `PRESERVE_ALL` 을 주장하지 마십시오 — 어댑터가 모듈 세대를 비교해 거짓 주장을
거부합니다.

함수 패스와 루프 패스는 동시에 실행될 수 있으므로, 변경 가능한 플러그인 상태는
플러그인이 선언한 `NevercConcurrencyModel` 을 따라야 합니다.

## 분석

일곱 가지 내장 분석을 ID 로 질의할 수 있습니다: `DOMINATOR_TREE`,
`POST_DOMINATOR_TREE`, `LOOP_INFO`, `SCALAR_EVOLUTION`, `MEMORY_SSA`,
`CALL_GRAPH`, `ALIAS`.

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

각각은 불투명한 덩어리가 아니라 타입이 붙은 접근자를 갖습니다:
`DominatorTreeDominates`, `GetLoopCount` / `GetLoopHeader` /
`GetLoopForBlock`, `GetScalarEvolutionConstantTripCount`,
`GetMemoryAccessKind`(`NONE`, `USE`, `DEF`, `PHI`, `LIVE_ON_ENTRY`),
`GetDirectCalleeCount` / `GetDirectCallee`, 그리고 `Alias`(`NO`, `MAY`,
`PARTIAL`, `MUST`).

플러그인 분석은 자체 수명 주기와 함께 등록합니다:

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

`Invalidate` 에는 이유가 전달됩니다 — `INVALIDATED_BY_PASS` 또는
`INVALIDATED_BY_PLAN_DESTROY`. 결과는 호출마다 캐시되고, 실행 중인 패스가 무엇을
보존했는지에 따라 버려집니다. 의존 순환은 등록 시점에 거부되고, 분석 콜백 안에서
IR 을 변경하는 것도 거부됩니다.

## 생성과 최적화 대체

`NevercIRGenAPI` 는 `neverc.ir.generate` 를 대체합니다:

```c
NevercIRGeneratePhaseInput In = {0};
In.Header = /* … */;
Gen->GetGeneratePhaseInput(Gen->Context, Frame, Frame->Input, &In);
/* In.SemanticUnit, .TargetTriple, .DataLayout, .SourceIdentity,
   .SourceDigest */

const NevercIRCoreAPI *Core;
const NevercIRBuilderAPI *Builders;
Gen->CreateModule(Gen->Context, Frame, SV("my.module"), &Core, &Builders);
/* … 모듈을 만든다 … */

NevercIRModuleArtifactDescriptor Descriptor = {0};
Descriptor.Header           = /* … */;
Descriptor.Product          = MyProductID;
Descriptor.DependencyDigest = Digest;
Gen->PublishModule(Gen->Context, Frame, &Descriptor, &Output);
```

`ImportModule` 은 빈 모듈 대신 비트코드나 텍스트 IR 에서 출발합니다.
`NevercIROptimizationAPI` 는 `neverc.ir.optimize` 에 대해 같은 모양이며, 들어오는
모듈에 닿는 `GetInputModule` 과 내장 파이프라인에 위임한 뒤 그 결과를 후처리하는
`RunBuiltinPipeline` 이 더해집니다.

두 경로 모두 포인터를 반환하는 대신 호스트를 통해 공개하고, 타깃 호환성을
검증하며, 공개에 실패하면 이전 모듈을 원자적으로 지킵니다. 그 뒤에도
`neverc.ir.final_verify` 는 어김없이 실행됩니다.

## 예제

| 파일 | 보여 주는 것 |
|---|---|
| `pluginsdk/examples/FunctionPass.c` | ABI 협상을 포함한 읽기 전용 함수 패스 |
| `pluginsdk/examples/ExamplePlugin.c` | 값 커서로 함수를 훑는 모듈 수준 패스 |
| `pluginsdk/examples/CustomCallConvPlugin.c` | 속성과 호출 지점 속성 |

```sh
cmake --build build-neverc --target neverc-plugin-example-function-pass
build-neverc/bin/neverc \
  -fplugin=build-neverc/neverc/pluginsdk/examples/host/FunctionPass.so \
  -O2 -c input.c -o input.o
```

CMake 가 여러분의 플랫폼에 맞게 만든 모듈 접미사를 쓰십시오.

## 규칙

- 모든 콜백에서 `NevercStatus` 를 반환하십시오. 플러그인의 실패는 구조화된 진단이
  됩니다. 예외가 C 경계를 넘게 하지 마십시오.
- 값을 채우는 호출 전에 모든 출력 구조체를 0 으로 초기화하고 `Header` 를
  설정하십시오.
- 옵코드, 타입, 속성의 수치를 하드코딩하지 마십시오. `PluginIRSchema.inc` 의
  이름을 쓰면 스키마 개정이 컴파일 오류가 됩니다.
- 모든 `BeginMutation` 은 정확히 하나의 `DestroyMutation` 에, 모든
  `CreateBuilder` 는 정확히 하나의 `DestroyBuilder` 에 대응해야 하며, 오류
  경로에서도 마찬가지입니다.
- `ExportModule` 이 건네준 것은 `ReleaseSerializedBuffer` 로 해제하십시오.
- IR 을 수정한 뒤에 `NEVERC_IR_PRESERVE_ALL` 을 주장하지 마십시오.
- 플러그인이 `NEVERC_CONCURRENCY_SESSION_SERIAL` 을 선언하지 않았다면 함수 패스와
  루프 패스가 병렬로 실행된다고 가정하십시오.
- `neverc.ir.final_verify` 는 봉인되어 있습니다. 플러그인이 무슨 짓을 해도 이를
  건너뛸 수 없습니다.

규범적 선언, 스키마 상수, 단계 정책, 테스트 증거는 `PluginIR.h`,
`Schema/PluginIRSchema.inc`, `Schema/PhaseSchema.json`, `coverage.json` 을
참조하십시오.
