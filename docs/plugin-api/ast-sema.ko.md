**언어**: [English](ast-sema.md) | [简体中文](ast-sema.zh-CN.md) | [繁體中文](ast-sema.zh-TW.md) | [日本語](ast-sema.ja.md) | [한국어](ast-sema.ko.md) | [Français](ast-sema.fr.md) | [Deutsch](ast-sema.de.md) | [Español](ast-sema.es.md) | [Italiano](ast-sema.it.md) | [Русский](ast-sema.ru.md) | [العربية](ast-sema.ar.md)

# NeverC 플러그인 AST 및 의미 분석 API

프런트엔드는 세 개의 테이블이 담당합니다. `NevercParserAPI` 는 체크포인트가
달린 토큰 커서를 몰아 파싱의 일부 — 새로운 선언 형태, 새로운 문 — 를 플러그인이
넘겨받게 해 줍니다. `NevercASTAPI` 는 트리를 읽고 트랜잭션 방식으로 변경합니다.
`NevercSemaAPI` 는 이름 탐색, 타입 구성, 변환 분류, 상수 평가를 맡습니다.

AST 는 Clang 클래스 계층을 C 로 옮긴 것이 아니라 **스키마** 를 통해 공개됩니다.
노드는 불투명한 핸들이며, 안정된 ID 로 속성을 요청하면 태그가 붙은
`NevercASTValue` 가 돌아옵니다. 바로 이 간접성이 이 표면을 LLVM 버전 사이에서
안정적으로 유지합니다.

## 인터페이스

```c
#include "neverc/Plugin/PluginAST.h"
#include "neverc/Plugin/PluginSema.h"
```

| 인터페이스 | 테이블 | 버전 매크로 |
|---|---|---|
| `NEVERC_INTERFACE_AST_{HIGH,LOW}` | `NevercASTAPI` | `NEVERC_AST_API_MAJOR`(1) / `_MINOR`(1) |
| `NEVERC_INTERFACE_PARSER_{HIGH,LOW}` | `NevercParserAPI` | `NEVERC_PARSER_API_MAJOR` / `_MINOR` |
| `NEVERC_INTERFACE_SEMA_{HIGH,LOW}` | `NevercSemaAPI` | `NEVERC_SEMA_API_MAJOR` / `_MINOR` |

`Schema/PluginASTSchema.inc` 가 노드 종류, 속성, 자식 슬롯 ID 를 공급하며, 그
capability major 는 반드시 `NEVERC_AST_API_MAJOR` 와 같아야 합니다.

## 단계

일곱 개의 구문 단계와 일곱 개의 의미 단계가 있으며, 모두
`OBSERVABLE | INTERCEPTABLE | REPLACEABLE` 입니다:

| 구문 | 의미 |
|---|---|
| `neverc.syntax.parse` | `neverc.sema.analyze` |
| `neverc.syntax.extension.declaration` | `neverc.sema.extension.declaration` |
| `neverc.syntax.extension.statement` | `neverc.sema.extension.statement` |
| `neverc.syntax.extension.expression` | `neverc.sema.extension.expression` |
| `neverc.syntax.extension.type_name` | `neverc.sema.extension.type` |
| `neverc.syntax.extension.attribute` | `neverc.sema.extension.lookup` |
| `neverc.syntax.extension.keyword` | `neverc.sema.extension.conversion` |

`neverc.syntax.parse` 는 토큰 스트림을 소비해 AST 단위를 만들고,
`neverc.sema.analyze` 는 그 단위를 소비해 의미 단위를 만듭니다. `extension.*`
단계는 언어 확장을 위한 훅입니다. 호스트는 내장 동작으로 물러나기 전에 이 구성을
처리하고 싶은 플러그인이 있는지 먼저 묻습니다.

## 스키마 모델

모든 노드는 `NevercASTNodeHandle` 이며, 타입이 붙은 별칭(`NevercDeclHandle`,
`NevercStmtHandle`, `NevercExprHandle`, `NevercTypeHandle`, `NevercAttrHandle`,
`NevercDeclContextHandle`, `NevercTypeLocHandle`)이 마련되어 있습니다. 구조
탐색은 균일합니다:

```c
NevercASTNodeInfo Info = {0};
Info.Header = (NevercABITableHeader){sizeof(Info), NEVERC_AST_API_MAJOR,
                                     NEVERC_AST_API_MINOR, 0};
AST->GetNodeInfo(AST->Context, Task, Node, &Info);
/* Info.Kind, .Domain, .Parent, .DeclContext, .SourceRange */

uint64_t ChildCount = 0;
AST->GetChildCount(AST->Context, Task, Node, &ChildCount);
for (uint64_t I = 0; I != ChildCount; ++I) {
  NevercASTNodeHandle Child;
  AST->GetChild(AST->Context, Task, Node, I, &Child);
}
```

`Domain` 은 `NEVERC_AST_SCHEMA_DOMAIN_DECL`, `STMT`, `TYPE`, `TYPE_LOC`, `ATTR`
중 하나입니다.

속성은 ID 로 태그가 붙은 값에 읽어 들입니다:

```c
typedef struct NevercASTValue {
  NevercABITableHeader Header;
  NevercASTValueType Type;
  uint32_t Reserved;
  int64_t SignedValue;
  uint64_t UnsignedValue;
  NevercStringView StringValue;
  NevercSourceRange SourceRangeValue;
  NevercASTNodeHandle NodeValue;
} NevercASTValue;
```

`Type` 이 어느 멤버가 유효한지를 고릅니다: `NEVERC_AST_VALUE_BOOL`, `I64`,
`U64`, `STRING`, `SOURCE_RANGE`, `NODE`, `DECL`, `STMT`, `EXPR`, `TYPE`,
`TYPE_LOC`, `ATTR`, `IDENTIFIER`, `ENUM`, `VERSION`, `PARAMETER_INDEX`,
`ALIGNMENT_OPERAND`. 스키마는 각 속성의 접근 방식(`READ_ONLY`, `READ_WRITE`,
`BUILD_ONLY`)과 개수(`REQUIRED`, `OPTIONAL`, `MANY`)를 기록하므로, 읽기 전용
속성에 쓰려는 시도는 트리를 망가뜨리는 대신 API 단계에서 실패합니다.

많은 노드를 한 번에 훑을 때는 배치 호출이 더 쌉니다. 출력 스트라이드를 받으므로
자신의 구조체 배열에 곧바로 쓸 수 있습니다:

```c
AST->GetNodeInfoBatch(AST->Context, Task, Nodes, NodeCount,
                      OutInfos, OutInfoCapacity, OutInfoStride);
AST->GetPropertyBatch(AST->Context, Task, Nodes, Properties, QueryCount,
                      OutValues, OutValueCapacity, OutValueStride);
```

## 타입이 붙은 접근자

플러그인이 가장 자주 다루는 구성에는 속성 조회 대신 직접 읽는 함수가 있습니다:

| 호출 | 얻는 것 |
|---|---|
| `GetTranslationUnit` | 루트 선언 |
| `GetFunctionDeclInfo`, `GetFunctionDeclParameter` | 이름, 타입, 반환 타입, 본문, 매개변수 개수, 가변 인자, 정의 |
| `GetVarDeclInfo` | 이름, 타입, 초기화식, 정의, 전역 저장 기간 |
| `GetRecordDeclInfo` | 이름, 필드 수, 완전성, 공용체 여부, 유연 배열 멤버 |
| `GetDeclAttributeCount`, `GetDeclAttribute`, `GetAttrInfo` | 속성 종류, 철자, 암시적, 상속됨 |
| `GetDeclRefExprInfo` | 참조된 선언과 발견된 선언, 타입 |
| `GetCallExprInfo`, `GetCallExprArgument` | 피호출자, 직접 피호출자, 타입, 인자 |
| `GetBinaryOperatorInfo` | 좌, 우, 타입, 연산자 철자와 종류 |
| `GetCompoundStmtInfo` | 문의 개수 |
| `GetIntegerLiteralInfo`, `GetIntegerLiteralWord` | 비트 폭과 리틀엔디언 워드 |
| `GetTypeInfo`, `GetTypeElement` | 타입의 완전한 서술 |
| `GetBuiltinType` | `NevercBuiltinTypeKind` 로 지정한 내장 타입 |

이 가운데 가장 풍부한 것이 `NevercTypeInfo` 입니다:

```c
typedef struct NevercTypeInfo {
  NevercABITableHeader Header;
  NevercTypeKind Kind;
  NevercTypeQualifierFlags QualifierFlags;  /* CONST, RESTRICT, VOLATILE, UNALIGNED */
  NevercTypeFlags Flags;                    /* CANONICAL, SUGARED, DEPENDENT,
                                               INCOMPLETE, FUNCTION, VARIADIC,
                                               HAS_KNOWN_LAYOUT, POINTER, ARRAY,
                                               VECTOR, ATOMIC */
  NevercTypeAddressSpaceKind AddressSpaceKind;
  uint32_t TargetAddressSpace;
  uint32_t Reserved;
  uint64_t SizeInBits;
  uint64_t AlignmentInBits;
  uint64_t ElementCount;
  NevercTypeHandle CanonicalType;
  NevercTypeHandle DesugaredType;
  NevercTypeHandle RelatedType;
  NevercStringView Name;
} NevercTypeInfo;
```

내장 타입 종류는 `NEVERC_BUILTIN_TYPE_VOID` 와 `_BOOL` 에서 정수 사다리를 거쳐
`_LONG_DOUBLE` 까지, 이항 연산자 종류는 `NEVERC_BINARY_OPERATOR_MUL` 에서
`_COMMA` 까지 이어집니다.

## 만들기와 변경하기

구성에는 빌더를, 변경에는 트랜잭션을 씁니다. 둘은 조합됩니다. 먼저 교체할 노드를
만들고, 그다음 끼워 넣습니다.

```c
NevercASTBuilderHandle Builder;
AST->CreateASTBuilder(AST->Context, Task, NodeKind, &Builder);

NevercASTValue Value = {0};
Value.Header = (NevercABITableHeader){sizeof(Value), NEVERC_AST_API_MAJOR,
                                      NEVERC_AST_API_MINOR, 0};
Value.Type          = NEVERC_AST_VALUE_U64;
Value.UnsignedValue = 1;
AST->ASTBuilderSetProperty(AST->Context, Task, Builder, PropertyID, &Value);
AST->ASTBuilderSetChild(AST->Context, Task, Builder, SlotID, 0, ChildNode);

NevercASTNodeHandle NewNode;
AST->ASTBuilderCommit(AST->Context, Task, Builder, &NewNode);
AST->DestroyASTBuilder(AST->Context, Task, Builder);
```

64비트보다 넓은 리터럴에는 `ASTBuilderSetIntegerValue` 가
`NevercAPIntView`(리틀엔디언 워드와 비트 폭)를 받고,
`ASTBuilderSetBinaryOperatorKind` 는 이항식의 연산자를 설정합니다.

```c
NevercASTMutationHandle Mutation;
AST->BeginASTMutation(AST->Context, Task, &Mutation);
AST->ASTMutationReplaceChild(AST->Context, Task, Mutation, Parent, SlotID,
                             Index, NewNode);
AST->CommitASTMutation(AST->Context, Task, Mutation);   /* 또는 AbortASTMutation */
AST->DestroyASTMutation(AST->Context, Task, Mutation);
```

커밋은 준비된 트리를 검증하고 원자적으로 공개합니다. 커밋이 실패하면 이전 트리가
그대로 남고, 중단하면 그 변경이 만든 핸들이 무효가 됩니다.
[`pluginsdk/examples/ASTRewritePlugin.c`](../../pluginsdk/examples/ASTRewritePlugin.c)
가 파서 가로채기를 포함한 전 과정을 보여 줍니다.

## 수명 주기 이벤트

폴링하는 대신, 프런트엔드가 선언을 공개하는 열한 개 지점을 구독하십시오:

```c
NevercASTLifecycleObserverDescriptor Observer = {0};
Observer.Header = /* … */;
Observer.Events =
    NEVERC_AST_LIFECYCLE_EVENT_MASK(NEVERC_AST_LIFECYCLE_TOP_LEVEL_DECL) |
    NEVERC_AST_LIFECYCLE_EVENT_MASK(NEVERC_AST_LIFECYCLE_TRANSLATION_UNIT);
Observer.Callback = on_lifecycle;
Observer.UserData = State;
AST->RegisterLifecycleObserver(AST->Context, Task, &Observer);
```

종류는 `TREE_INITIALIZE`, `SEMA_BEGIN`, `TOP_LEVEL_DECL`,
`INLINE_FUNCTION_DEFINITION`, `INTERESTING_DECL`, `TAG_DEFINITION`,
`TAG_REQUIRED_DEFINITION`, `TENTATIVE_DEFINITION`, `EXTERNAL_DECLARATION`,
`TRANSLATION_UNIT`, `SEMA_END` 이고, `NEVERC_AST_LIFECYCLE_EVENT_MASK_ALL` 이
열한 가지 모두를 덮습니다. 이벤트는 번역 단위, 단일 선언, 선언 배열을 실어
나르며 — 모두 읽기 전용이고 콜백 동안만 빌려온 것입니다.

## 파서 확장

파서 확장은 추측 파싱이 내장된 토큰 커서를 받습니다:

```c
NevercParserExtensionInput In = {0};
In.Header = /* … */;
Parser->GetExtensionInput(Parser->Context, Frame, Frame->Input, &In);

NevercParserCheckpointHandle Checkpoint;
Parser->CursorCheckpoint(Parser->Context, Task, In.Cursor, &Checkpoint);

NevercTokenHandle Token;
Parser->CursorPeek(Parser->Context, Task, In.Cursor, /*Offset=*/0, &Token);
if (!is_my_construct(Token)) {
  Parser->CursorRollback(Parser->Context, Task, In.Cursor, Checkpoint);
  Out.Disposition = NEVERC_PARSER_EXTENSION_UNHANDLED;
} else {
  Parser->CursorConsume(Parser->Context, Task, In.Cursor, &Token);
  /* … 노드를 만든다 … */
  Parser->CursorCommit(Parser->Context, Task, In.Cursor, Checkpoint);
  Out.Disposition = NEVERC_PARSER_EXTENSION_HANDLED;
  Out.ResultKind  = NEVERC_PARSER_RESULT_DECL;
  Out.Node        = MyNode;
}
Parser->CreateExtensionOutput(Parser->Context, Frame, Continuation, &Out,
                              &Output);
```

입력의 `ExpectedResult` 가 파서가 무엇을 필요로 하는지 알려 줍니다:
`NEVERC_PARSER_RESULT_DECL`, `STMT`, `EXPR`, `TYPE`, `ATTRIBUTE`.
`CreateParsedAttribute` 는 GNU(`__attribute__`), C23(`[[…]]`), `__declspec`
형태로 속성을 만듭니다.

`neverc.syntax.parse` 자체의 제공자는 AST 단위 전체를 공개합니다:

```c
NevercParserASTUnitDescriptor Unit = {0};
Unit.Header          = /* … */;
Unit.Product         = (NevercInterfaceID){NEVERC_AST_PRODUCT_STANDARD_HIGH,
                                           NEVERC_AST_PRODUCT_STANDARD_LOW};
Unit.TranslationUnit = TU;
Parser->CreateASTUnit(Parser->Context, Frame, &Unit, &Output);
```

`GetASTUnitInfo` 는 그 단위의 `SemanticState` 를 보고합니다.
`NEVERC_AST_UNIT_UNANALYZED` 로 공개된 단위는 의미 분석을 거쳐 재생됩니다.
`NEVERC_AST_UNIT_SEMANTICALLY_ANALYZED` 는 제공자가 그 작업을 이미 끝냈다고
주장하는 것입니다.

## 의미 질의

```c
NevercSemaLookupRequest Request = {0};
Request.Header = /* … */;
Request.Scope  = Scope;
Request.Name   = SV("my_symbol");
Request.Kind   = NEVERC_SEMA_LOOKUP_ORDINARY;   /* 또는 _TAG, _MEMBER */

NevercLookupResultHandle Result;
Sema->LookupName(Sema->Context, Task, &Request, &Result);

NevercSemaLookupResultInfo Info = {0};
Info.Header = /* … */;
Sema->GetLookupResultInfo(Sema->Context, Task, Result, &Info);
/* Info.Kind 는 NOT_FOUND, FOUND, AMBIGUOUS; 이어서 Info.CandidateCount. */

for (uint64_t I = 0; I != Info.CandidateCount; ++I) {
  NevercDeclHandle Candidate;
  Sema->GetLookupCandidate(Sema->Context, Task, Result, I, &Candidate);
}
Sema->DestroyLookupResult(Sema->Context, Task, Result);
```

`GetCurrentScope`, `GetScopeInfo`, `GetScopeDeclaration` 이 스코프 사슬을
따라갑니다. 스코프 플래그는 `NEVERC_SEMA_SCOPE_FILE`, `FUNCTION`, `RECORD`,
`BLOCK` 입니다.

상수 평가는 값의 형태를 서술하는 정보를 지닌 핸들을 반환합니다:

```c
NevercConstantValueHandle Value;
Sema->EvaluateConstant(Sema->Context, Task, Expression, &Value);

NevercSemaConstantValueInfo Info = {0};
Info.Header = /* … */;
Sema->GetConstantValueInfo(Sema->Context, Task, Value, &Info);
/* Info.Kind: NONE, INDETERMINATE, INTEGER, FLOAT, FIXED_POINT,
   COMPLEX_INTEGER, COMPLEX_FLOAT, ADDRESS, VECTOR, ARRAY, STRUCT, UNION,
   ADDRESS_LABEL_DIFFERENCE. */

uint64_t Word;
Sema->GetConstantIntegerWord(Sema->Context, Task, Value, 0, &Word);
Sema->DestroyConstantValue(Sema->Context, Task, Value);
```

변환은 적용되기 전에 분류되므로, 플러그인이 그 결정을 들여다볼 수 있습니다:

```c
NevercConversionSequenceHandle Sequence;
Sema->ClassifyImplicitConversion(Sema->Context, Task, SourceType, DestType,
                                 &Sequence);
NevercSemaConversionSequenceInfo SeqInfo = {0};
SeqInfo.Header = /* … */;
Sema->GetConversionSequenceInfo(Sema->Context, Task, Sequence, &SeqInfo);
/* SeqInfo.Kind 는 COMPATIBLE, POINTER_TO_INTEGER,
   INTEGER_TO_POINTER, INCOMPATIBLE_POINTER, DISCARDS_QUALIFIERS,
   ADDRESS_SPACE_MISMATCH, VECTOR, INCOMPATIBLE 등에 걸쳐 있고,
   이어서 SeqInfo.Viable 과 .RequiresDiagnostic 이 온다. */
```

`AreTypesCompatible`, `GetCanonicalType`, `GetTagType`, `GetBuiltinInfo` 가
읽기 전용 표면을 마무리합니다.

## 변경 리스

의미 상태를 바꾸는 모든 것 — 타입 생성, 변환 적용, 의미 진단 발행 — 에는
리스(lease)가 필요합니다. 리스야말로 동시 의미 작업을 안전하게 만드는 장치입니다:

```c
NevercSemaMutationLeaseHandle Lease;
Sema->AcquireMutationLease(Sema->Context, Task, &Lease);

NevercTypeHandle Pointer;
Sema->CreatePointerType(Sema->Context, Task, Lease, Pointee, &Pointer);

NevercExprHandle Converted;
Sema->ApplyImplicitConversion(Sema->Context, Task, Lease, Sequence,
                              Expression, NEVERC_SEMA_CONVERSION_ARGUMENT,
                              &Converted);

Sema->ReleaseMutationLease(Sema->Context, Task, Lease);
```

`CreateConstantArrayType`, `CreateFunctionType`, `CreateAtomicType`,
`CreateVectorType`, `CreateExplicitCast`, `EmitDiagnostic` 은 모두 리스를
받습니다. 변환 맥락은 `NEVERC_SEMA_CONVERSION_ASSIGNMENT`, `ARGUMENT`,
`RETURN`, `INITIALIZATION`, `EXPLICIT_CAST` 입니다.

## 의미 확장 단계

각 확장 단계에는 짝이 되는 입력/출력이 있습니다. 예를 들어 표현식 훅은 이렇습니다:

```c
NevercSemaExpressionExtensionInput In = {0};
In.Header = /* … */;
Sema->GetExpressionExtensionInput(Sema->Context, Frame, Frame->Input, &In);
/* In.Left, In.Right, In.OperatorLocation */

NevercSemaExpressionExtensionOutput Out = {0};
Out.Header      = In.Header;
Out.Disposition = NEVERC_SEMA_EXTENSION_HANDLED;   /* 또는 _UNHANDLED */
Out.Expression  = Result;
Sema->CreateExpressionExtensionOutput(Sema->Context, Frame, Continuation,
                                      &Out, &Output);
```

같은 모양이 `Statement`, `Declaration`, `Type`, `Lookup`, `Conversion` 에도
적용됩니다. `NEVERC_SEMA_EXTENSION_UNHANDLED` 를 반환하면 내장 동작이 실행됩니다.

`neverc.sema.analyze` 의 제공자는 의미 단위를 공개합니다:

```c
NevercSemanticUnitDescriptor Unit = {0};
Unit.Header           = /* … */;
Unit.Product          = (NevercInterfaceID){NEVERC_SEMANTIC_PRODUCT_STANDARD_HIGH,
                                            NEVERC_SEMANTIC_PRODUCT_STANDARD_LOW};
Unit.TranslationUnit  = TU;
Unit.SemanticComplete = NEVERC_TRUE;
Sema->CreateSemanticUnit(Sema->Context, Frame, &Unit, &Output);
```

`GetSemanticUnitInfo` 는 `DiagnosticState`
(`NEVERC_SEMANTIC_DIAGNOSTICS_CLEAN` 또는 `_HAS_ERROR`), 그 단위가 재생되었는지
여부, 그리고 검증기 요약을 보고합니다.

## 규칙

- AST 와 타입 핸들은 태스크 범위입니다. 콜백 밖으로 저장하지 마십시오.
- 모든 빌더, 변경, 탐색 결과, 변환 시퀀스, 상수 값에는 짝이 되는 `Destroy*` 가
  있습니다. 오류 경로에서도 호출하십시오.
- 리스 없는 의미 변경은 `NEVERC_STATUS_INVALID_STATE` 를 반환합니다.
- 수명 주기 관찰자에서 트리를 변경하지 마십시오 — 관찰자는 읽기 전용입니다.
  해당 단계의 인터셉터를 쓰십시오.
- 속성 ID 와 자식 슬롯 ID 는 스키마 상수입니다. 숫자 리터럴을 하드코딩하지 말고
  `PluginASTSchema.inc` 의 이름을 쓰십시오. 그래야 스키마 개정이 컴파일 오류가
  됩니다.
- `SizeInBits` 나 `AlignmentInBits` 를 믿기 전에 `NevercTypeInfo.Flags` 에
  `HAS_KNOWN_LAYOUT` 이 있는지 확인하십시오.

규범적 선언은 `PluginAST.h`, `PluginSema.h`, `Schema/ASTSchema.json` 을,
동작하는 파서 가로채기와 원자적 트리 재작성은
`pluginsdk/examples/ASTRewritePlugin.c` 를 참조하십시오.
