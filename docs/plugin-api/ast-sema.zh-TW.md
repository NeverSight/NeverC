**語言**: [English](ast-sema.md) | [简体中文](ast-sema.zh-CN.md) | [繁體中文](ast-sema.zh-TW.md) | [日本語](ast-sema.ja.md) | [한국어](ast-sema.ko.md) | [Français](ast-sema.fr.md) | [Deutsch](ast-sema.de.md) | [Español](ast-sema.es.md) | [Italiano](ast-sema.it.md) | [Русский](ast-sema.ru.md) | [العربية](ast-sema.ar.md)

[← NeverC 外掛 ABI](README.zh-TW.md)

# NeverC 外掛 AST 與語意 API

三張表涵蓋整個前端。`NevercParserAPI` 讓外掛接手一部分剖析工作──一種新的宣告
形式、一種新的陳述式──做法是驅動一個帶檢查點的 token 游標。`NevercASTAPI` 讀
取語法樹並以交易方式變更它。`NevercSemaAPI` 負責查找、型別建構、轉換分類與常數
求值。

AST 是透過 **schema** 公開的，而不是把 Clang 的類別階層鏡射成 C。節點是不透明的
控制代碼；你用穩定的 ID 去要一個屬性，拿回一個帶標籤的 `NevercASTValue`。正是這
一點讓這個介面能跨越 LLVM 版本保持穩定。

## 介面

```c
#include "neverc/Plugin/PluginAST.h"
#include "neverc/Plugin/PluginSema.h"
```

| 介面 | 表 | 版本巨集 |
|---|---|---|
| `NEVERC_INTERFACE_AST_{HIGH,LOW}` | `NevercASTAPI` | `NEVERC_AST_API_MAJOR`（1）/ `_MINOR`（1） |
| `NEVERC_INTERFACE_PARSER_{HIGH,LOW}` | `NevercParserAPI` | `NEVERC_PARSER_API_MAJOR` / `_MINOR` |
| `NEVERC_INTERFACE_SEMA_{HIGH,LOW}` | `NevercSemaAPI` | `NEVERC_SEMA_API_MAJOR` / `_MINOR` |

`Schema/PluginASTSchema.inc` 提供節點種類、屬性與子槽的 ID；它的 capability
major 必須等於 `NEVERC_AST_API_MAJOR`。

## 階段

七個語法階段與七個語意階段，全都是
`OBSERVABLE | INTERCEPTABLE | REPLACEABLE`：

| 語法 | 語意 |
|---|---|
| `neverc.syntax.parse` | `neverc.sema.analyze` |
| `neverc.syntax.extension.declaration` | `neverc.sema.extension.declaration` |
| `neverc.syntax.extension.statement` | `neverc.sema.extension.statement` |
| `neverc.syntax.extension.expression` | `neverc.sema.extension.expression` |
| `neverc.syntax.extension.type_name` | `neverc.sema.extension.type` |
| `neverc.syntax.extension.attribute` | `neverc.sema.extension.lookup` |
| `neverc.syntax.extension.keyword` | `neverc.sema.extension.conversion` |

`neverc.syntax.parse` 吃進 token 串流並產出 AST 單元；`neverc.sema.analyze` 吃進
那個單元並產出語意單元。`extension.*` 階段是語言擴充的掛鉤：在退回內建行為之前，
主機會先問有沒有哪個外掛想處理這個結構。

## Schema 模型

每個節點都是 `NevercASTNodeHandle`，並有具型別的別名（`NevercDeclHandle`、
`NevercStmtHandle`、`NevercExprHandle`、`NevercTypeHandle`、`NevercAttrHandle`、
`NevercDeclContextHandle`、`NevercTypeLocHandle`）。結構性巡覽是統一的：

```c
NevercASTNodeInfo Info = {0};
Info.Header = (NevercABITableHeader){sizeof(Info), NEVERC_AST_API_MAJOR,
                                     NEVERC_AST_API_MINOR, 0};
AST->GetNodeInfo(AST->Context, Task, Node, &Info);
/* Info.Kind、.Domain、.Parent、.DeclContext、.SourceRange */

uint64_t ChildCount = 0;
AST->GetChildCount(AST->Context, Task, Node, &ChildCount);
for (uint64_t I = 0; I != ChildCount; ++I) {
  NevercASTNodeHandle Child;
  AST->GetChild(AST->Context, Task, Node, I, &Child);
}
```

`Domain` 是 `NEVERC_AST_SCHEMA_DOMAIN_DECL`、`STMT`、`TYPE`、`TYPE_LOC` 或
`ATTR` 其中之一。

屬性以 ID 讀進一個帶標籤的值：

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

`Type` 決定哪個成員有效：`NEVERC_AST_VALUE_BOOL`、`I64`、`U64`、`STRING`、
`SOURCE_RANGE`、`NODE`、`DECL`、`STMT`、`EXPR`、`TYPE`、`TYPE_LOC`、`ATTR`、
`IDENTIFIER`、`ENUM`、`VERSION`、`PARAMETER_INDEX` 或 `ALIGNMENT_OPERAND`。
schema 記錄了每個屬性的存取模式（`READ_ONLY`、`READ_WRITE`、`BUILD_ONLY`）與基數
（`REQUIRED`、`OPTIONAL`、`MANY`），所以試圖寫入唯讀屬性會在 API 層失敗，而不是
把語法樹弄壞。

一次巡覽很多節點時，批次呼叫比較划算；它們接受一個輸出間距，讓你可以直接寫進自己
的結構陣列：

```c
AST->GetNodeInfoBatch(AST->Context, Task, Nodes, NodeCount,
                      OutInfos, OutInfoCapacity, OutInfoStride);
AST->GetPropertyBatch(AST->Context, Task, Nodes, Properties, QueryCount,
                      OutValues, OutValueCapacity, OutValueStride);
```

## 具型別的存取器

對於外掛最常碰的那些結構，有直接的讀取器，不必走屬性查找：

| 呼叫 | 產出 |
|---|---|
| `GetTranslationUnit` | 根宣告 |
| `GetFunctionDeclInfo`、`GetFunctionDeclParameter` | 名稱、型別、回傳型別、本體、參數數量、可變參數、定義 |
| `GetVarDeclInfo` | 名稱、型別、初始設定式、定義、全域儲存期 |
| `GetRecordDeclInfo` | 名稱、欄位數、是否完整、是否 union、彈性陣列成員 |
| `GetDeclAttributeCount`、`GetDeclAttribute`、`GetAttrInfo` | 屬性種類、拼寫、隱含、繼承 |
| `GetDeclRefExprInfo` | 被參照與被找到的宣告、型別 |
| `GetCallExprInfo`、`GetCallExprArgument` | 被呼叫者、直接被呼叫者、型別、引數 |
| `GetBinaryOperatorInfo` | 左、右、型別、運算子拼寫與種類 |
| `GetCompoundStmtInfo` | 陳述式數量 |
| `GetIntegerLiteralInfo`、`GetIntegerLiteralWord` | 位元寬度與小端序字組 |
| `GetTypeInfo`、`GetTypeElement` | 完整型別描述 |
| `GetBuiltinType` | 依 `NevercBuiltinTypeKind` 取得內建型別 |

其中內容最豐富的是 `NevercTypeInfo`：

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

內建型別種類從 `NEVERC_BUILTIN_TYPE_VOID` 與 `_BOOL` 一路沿整數階梯到
`_LONG_DOUBLE`，二元運算子種類則從 `NEVERC_BINARY_OPERATOR_MUL` 到 `_COMMA`。

## 建構與變更

建構用建構器，變更用交易。兩者可以組合：先建好替換節點，再把它換進去。

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

對於寬度超過 64 位元的字面值，`ASTBuilderSetIntegerValue` 接受一個
`NevercAPIntView`（小端序字組加上位元寬度）；`ASTBuilderSetBinaryOperatorKind`
則設定二元運算式的運算子。

```c
NevercASTMutationHandle Mutation;
AST->BeginASTMutation(AST->Context, Task, &Mutation);
AST->ASTMutationReplaceChild(AST->Context, Task, Mutation, Parent, SlotID,
                             Index, NewNode);
AST->CommitASTMutation(AST->Context, Task, Mutation);   /* 或 AbortASTMutation */
AST->DestroyASTMutation(AST->Context, Task, Mutation);
```

提交會驗證暫存的樹並原子性地發布。提交失敗會讓先前的樹保持完好，而中止則會讓該
變更所建立的控制代碼失效。
[`pluginsdk/examples/ASTRewritePlugin.c`](../../pluginsdk/examples/ASTRewritePlugin.c)
展示了整個循環，包含剖析器攔截。

## 生命週期事件

與其輪詢，不如訂閱前端發布宣告的那十一個時機點：

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

種類有 `TREE_INITIALIZE`、`SEMA_BEGIN`、`TOP_LEVEL_DECL`、
`INLINE_FUNCTION_DEFINITION`、`INTERESTING_DECL`、`TAG_DEFINITION`、
`TAG_REQUIRED_DEFINITION`、`TENTATIVE_DEFINITION`、`EXTERNAL_DECLARATION`、
`TRANSLATION_UNIT` 與 `SEMA_END`；`NEVERC_AST_LIFECYCLE_EVENT_MASK_ALL` 涵蓋全部
十一種。事件帶著翻譯單元、單一宣告以及一個宣告陣列──全都是唯讀，且只在該回呼期
間借用。

## 剖析器擴充

剖析器擴充會拿到一個內建推測式剖析能力的 token 游標：

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
  /* … 建構一個節點 … */
  Parser->CursorCommit(Parser->Context, Task, In.Cursor, Checkpoint);
  Out.Disposition = NEVERC_PARSER_EXTENSION_HANDLED;
  Out.ResultKind  = NEVERC_PARSER_RESULT_DECL;
  Out.Node        = MyNode;
}
Parser->CreateExtensionOutput(Parser->Context, Frame, Continuation, &Out,
                              &Output);
```

輸入上的 `ExpectedResult` 告訴你剖析器需要什麼：`NEVERC_PARSER_RESULT_DECL`、
`STMT`、`EXPR`、`TYPE` 或 `ATTRIBUTE`。`CreateParsedAttribute` 可以建構 GNU
（`__attribute__`）、C23（`[[…]]`）或 `__declspec` 形式的屬性。

`neverc.syntax.parse` 本身的 Provider 會發布一整個 AST 單元：

```c
NevercParserASTUnitDescriptor Unit = {0};
Unit.Header          = /* … */;
Unit.Product         = (NevercInterfaceID){NEVERC_AST_PRODUCT_STANDARD_HIGH,
                                           NEVERC_AST_PRODUCT_STANDARD_LOW};
Unit.TranslationUnit = TU;
Parser->CreateASTUnit(Parser->Context, Frame, &Unit, &Output);
```

`GetASTUnitInfo` 會回報該單元的 `SemanticState`。以
`NEVERC_AST_UNIT_UNANALYZED` 發布的單元會被送去重播語意分析；
`NEVERC_AST_UNIT_SEMANTICALLY_ANALYZED` 則主張 Provider 已經做完那份工作。

## 語意查詢

```c
NevercSemaLookupRequest Request = {0};
Request.Header = /* … */;
Request.Scope  = Scope;
Request.Name   = SV("my_symbol");
Request.Kind   = NEVERC_SEMA_LOOKUP_ORDINARY;   /* 或 _TAG、_MEMBER */

NevercLookupResultHandle Result;
Sema->LookupName(Sema->Context, Task, &Request, &Result);

NevercSemaLookupResultInfo Info = {0};
Info.Header = /* … */;
Sema->GetLookupResultInfo(Sema->Context, Task, Result, &Info);
/* Info.Kind 是 NOT_FOUND、FOUND 或 AMBIGUOUS；接著是 Info.CandidateCount。 */

for (uint64_t I = 0; I != Info.CandidateCount; ++I) {
  NevercDeclHandle Candidate;
  Sema->GetLookupCandidate(Sema->Context, Task, Result, I, &Candidate);
}
Sema->DestroyLookupResult(Sema->Context, Task, Result);
```

`GetCurrentScope`、`GetScopeInfo` 與 `GetScopeDeclaration` 可以走訪範圍鏈；範圍
旗標有 `NEVERC_SEMA_SCOPE_FILE`、`FUNCTION`、`RECORD` 與 `BLOCK`。

常數求值會回傳一個控制代碼，其資訊描述該值的形狀：

```c
NevercConstantValueHandle Value;
Sema->EvaluateConstant(Sema->Context, Task, Expression, &Value);

NevercSemaConstantValueInfo Info = {0};
Info.Header = /* … */;
Sema->GetConstantValueInfo(Sema->Context, Task, Value, &Info);
/* Info.Kind: NONE、INDETERMINATE、INTEGER、FLOAT、FIXED_POINT、
   COMPLEX_INTEGER、COMPLEX_FLOAT、ADDRESS、VECTOR、ARRAY、STRUCT、UNION、
   ADDRESS_LABEL_DIFFERENCE。 */

uint64_t Word;
Sema->GetConstantIntegerWord(Sema->Context, Task, Value, 0, &Word);
Sema->DestroyConstantValue(Sema->Context, Task, Value);
```

轉換會在套用之前先被分類，所以外掛可以檢視這個決策：

```c
NevercConversionSequenceHandle Sequence;
Sema->ClassifyImplicitConversion(Sema->Context, Task, SourceType, DestType,
                                 &Sequence);
NevercSemaConversionSequenceInfo SeqInfo = {0};
SeqInfo.Header = /* … */;
Sema->GetConversionSequenceInfo(Sema->Context, Task, Sequence, &SeqInfo);
/* SeqInfo.Kind 涵蓋 COMPATIBLE、POINTER_TO_INTEGER、
   INTEGER_TO_POINTER、INCOMPATIBLE_POINTER、DISCARDS_QUALIFIERS、
   ADDRESS_SPACE_MISMATCH、VECTOR、INCOMPATIBLE 等；
   接著是 SeqInfo.Viable 與 .RequiresDiagnostic。 */
```

`AreTypesCompatible`、`GetCanonicalType`、`GetTagType` 與 `GetBuiltinInfo` 補完了
唯讀的那一面。

## 變更租約

任何會改動語意狀態的動作──建立型別、套用轉換、發出語意診斷──都需要一份租約
（lease）。租約正是讓並行語意工作得以安全的關鍵：

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

`CreateConstantArrayType`、`CreateFunctionType`、`CreateAtomicType`、
`CreateVectorType`、`CreateExplicitCast` 與 `EmitDiagnostic` 全都要吃這份租約。
轉換脈絡有 `NEVERC_SEMA_CONVERSION_ASSIGNMENT`、`ARGUMENT`、`RETURN`、
`INITIALIZATION` 與 `EXPLICIT_CAST`。

## 語意擴充階段

每個擴充階段都有配對的輸入／輸出。以運算式掛鉤為例：

```c
NevercSemaExpressionExtensionInput In = {0};
In.Header = /* … */;
Sema->GetExpressionExtensionInput(Sema->Context, Frame, Frame->Input, &In);
/* In.Left、In.Right、In.OperatorLocation */

NevercSemaExpressionExtensionOutput Out = {0};
Out.Header      = In.Header;
Out.Disposition = NEVERC_SEMA_EXTENSION_HANDLED;   /* 或 _UNHANDLED */
Out.Expression  = Result;
Sema->CreateExpressionExtensionOutput(Sema->Context, Frame, Continuation,
                                      &Out, &Output);
```

同樣的形狀也適用於 `Statement`、`Declaration`、`Type`、`Lookup` 與 `Conversion`。
回傳 `NEVERC_SEMA_EXTENSION_UNHANDLED` 會讓內建行為繼續執行。

`neverc.sema.analyze` 的 Provider 則發布語意單元：

```c
NevercSemanticUnitDescriptor Unit = {0};
Unit.Header           = /* … */;
Unit.Product          = (NevercInterfaceID){NEVERC_SEMANTIC_PRODUCT_STANDARD_HIGH,
                                            NEVERC_SEMANTIC_PRODUCT_STANDARD_LOW};
Unit.TranslationUnit  = TU;
Unit.SemanticComplete = NEVERC_TRUE;
Sema->CreateSemanticUnit(Sema->Context, Frame, &Unit, &Output);
```

`GetSemanticUnitInfo` 會回報 `DiagnosticState`
（`NEVERC_SEMANTIC_DIAGNOSTICS_CLEAN` 或 `_HAS_ERROR`）、該單元是否被重播過，以
及一份驗證器摘要。

## 規則

- AST 與型別控制代碼的範圍是任務。絕不要把它存到回呼之後。
- 每一個建構器、變更、查找結果、轉換序列與常數值都有對應的 `Destroy*`；錯誤路徑
  上也要呼叫。
- 沒有租約就做語意變更會回傳 `NEVERC_STATUS_INVALID_STATE`。
- 不要從生命週期觀察者裡變更語法樹──觀察者是唯讀的。請改用對應階段上的攔截
  器。
- 屬性 ID 與子槽 ID 都是 schema 常數。不要寫死數字字面值；請用
  `PluginASTSchema.inc` 裡的名稱，這樣 schema 一旦改版就會變成編譯錯誤。
- 在信任 `SizeInBits` 或 `AlignmentInBits` 之前，先檢查 `NevercTypeInfo.Flags`
  是否帶有 `HAS_KNOWN_LAYOUT`。

規範性宣告請見 `PluginAST.h`、`PluginSema.h` 與 `Schema/ASTSchema.json`；可運作
的剖析器攔截與原子性樹重寫範例請見
`pluginsdk/examples/ASTRewritePlugin.c`。
