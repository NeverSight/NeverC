**Languages**: [English](ast-sema.md) | [简体中文](ast-sema.zh-CN.md) | [繁體中文](ast-sema.zh-TW.md) | [日本語](ast-sema.ja.md) | [한국어](ast-sema.ko.md) | [Français](ast-sema.fr.md) | [Deutsch](ast-sema.de.md) | [Español](ast-sema.es.md) | [Italiano](ast-sema.it.md) | [Русский](ast-sema.ru.md) | [العربية](ast-sema.ar.md)

[← NeverC Plugin ABI](README.md)

# NeverC Plugin AST and Semantic API

Three tables cover the front end. `NevercParserAPI` lets a plugin take over a
piece of parsing — a new declaration form, a new statement — by driving a
token cursor with checkpoints. `NevercASTAPI` reads and transactionally
mutates the tree. `NevercSemaAPI` does lookup, type construction, conversion
classification, and constant evaluation.

The AST is exposed through a **schema**, not through a C mirror of Clang's
class hierarchy. Nodes are opaque handles; you ask for a property by stable
ID and get a tagged `NevercASTValue` back. That is what makes the surface
stable across LLVM versions.

## Interfaces

```c
#include "neverc/Plugin/PluginAST.h"
#include "neverc/Plugin/PluginSema.h"
```

| Interface | Table | Version macros |
|---|---|---|
| `NEVERC_INTERFACE_AST_{HIGH,LOW}` | `NevercASTAPI` | `NEVERC_AST_API_MAJOR` (1) / `_MINOR` (1) |
| `NEVERC_INTERFACE_PARSER_{HIGH,LOW}` | `NevercParserAPI` | `NEVERC_PARSER_API_MAJOR` / `_MINOR` |
| `NEVERC_INTERFACE_SEMA_{HIGH,LOW}` | `NevercSemaAPI` | `NEVERC_SEMA_API_MAJOR` / `_MINOR` |

`Schema/PluginASTSchema.inc` supplies the node-kind, property, and child-slot
IDs; its capability major must equal `NEVERC_AST_API_MAJOR`.

## Phases

Seven syntax phases and seven semantic phases, all
`OBSERVABLE | INTERCEPTABLE | REPLACEABLE`:

| Syntax | Semantic |
|---|---|
| `neverc.syntax.parse` | `neverc.sema.analyze` |
| `neverc.syntax.extension.declaration` | `neverc.sema.extension.declaration` |
| `neverc.syntax.extension.statement` | `neverc.sema.extension.statement` |
| `neverc.syntax.extension.expression` | `neverc.sema.extension.expression` |
| `neverc.syntax.extension.type_name` | `neverc.sema.extension.type` |
| `neverc.syntax.extension.attribute` | `neverc.sema.extension.lookup` |
| `neverc.syntax.extension.keyword` | `neverc.sema.extension.conversion` |

`neverc.syntax.parse` consumes a token stream and produces an AST unit;
`neverc.sema.analyze` consumes that unit and produces a semantic unit. The
`extension.*` phases are the hooks for language extensions: the host asks
whether any plugin wants to handle this construct before falling back.

## The schema model

Every node is a `NevercASTNodeHandle`, with typed aliases
(`NevercDeclHandle`, `NevercStmtHandle`, `NevercExprHandle`,
`NevercTypeHandle`, `NevercAttrHandle`, `NevercDeclContextHandle`,
`NevercTypeLocHandle`). Structural navigation is uniform:

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

`Domain` is one of `NEVERC_AST_SCHEMA_DOMAIN_DECL`, `STMT`, `TYPE`,
`TYPE_LOC`, or `ATTR`.

Properties are read by ID into a tagged value:

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

`Type` selects which member is live: `NEVERC_AST_VALUE_BOOL`, `I64`, `U64`,
`STRING`, `SOURCE_RANGE`, `NODE`, `DECL`, `STMT`, `EXPR`, `TYPE`, `TYPE_LOC`,
`ATTR`, `IDENTIFIER`, `ENUM`, `VERSION`, `PARAMETER_INDEX`, or
`ALIGNMENT_OPERAND`. The schema records each property's access mode
(`READ_ONLY`, `READ_WRITE`, `BUILD_ONLY`) and cardinality (`REQUIRED`,
`OPTIONAL`, `MANY`), so an attempt to write a read-only property fails at the
API rather than corrupting the tree.

Walking many nodes at once is cheaper through the batch calls, which take an
output stride so you can write straight into your own array of structs:

```c
AST->GetNodeInfoBatch(AST->Context, Task, Nodes, NodeCount,
                      OutInfos, OutInfoCapacity, OutInfoStride);
AST->GetPropertyBatch(AST->Context, Task, Nodes, Properties, QueryCount,
                      OutValues, OutValueCapacity, OutValueStride);
```

## Typed accessors

For the constructs plugins touch most, there are direct readers rather than
property lookups:

| Call | Yields |
|---|---|
| `GetTranslationUnit` | The root declaration |
| `GetFunctionDeclInfo`, `GetFunctionDeclParameter` | Name, type, return type, body, parameter count, variadic, definition |
| `GetVarDeclInfo` | Name, type, initializer, definition, global storage |
| `GetRecordDeclInfo` | Name, field count, complete, union, flexible array member |
| `GetDeclAttributeCount`, `GetDeclAttribute`, `GetAttrInfo` | Attribute kind, spelling, implicit, inherited |
| `GetDeclRefExprInfo` | Referenced and found declaration, type |
| `GetCallExprInfo`, `GetCallExprArgument` | Callee, direct callee, type, arguments |
| `GetBinaryOperatorInfo` | Left, right, type, operator spelling and kind |
| `GetCompoundStmtInfo` | Statement count |
| `GetIntegerLiteralInfo`, `GetIntegerLiteralWord` | Bit width and little-endian words |
| `GetTypeInfo`, `GetTypeElement` | Full type description |
| `GetBuiltinType` | A builtin type by `NevercBuiltinTypeKind` |

`NevercTypeInfo` is the richest of these:

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

The builtin type kinds run from `NEVERC_BUILTIN_TYPE_VOID` and `_BOOL`
through the integer ladder to `_LONG_DOUBLE`, and the binary operator kinds
from `NEVERC_BINARY_OPERATOR_MUL` through `_COMMA`.

## Building and mutating

Construction uses a builder; mutation uses a transaction. They compose: build
the replacement node first, then swap it in.

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

`ASTBuilderSetIntegerValue` takes a `NevercAPIntView` (little-endian words
plus bit width) for literals wider than 64 bits, and
`ASTBuilderSetBinaryOperatorKind` sets the operator of a binary expression.

```c
NevercASTMutationHandle Mutation;
AST->BeginASTMutation(AST->Context, Task, &Mutation);
AST->ASTMutationReplaceChild(AST->Context, Task, Mutation, Parent, SlotID,
                             Index, NewNode);
AST->CommitASTMutation(AST->Context, Task, Mutation);   /* or AbortASTMutation */
AST->DestroyASTMutation(AST->Context, Task, Mutation);
```

Commit verifies the staged tree and publishes atomically. A failed commit
leaves the previous tree intact, and an abort makes the handles the mutation
created stale.
[`pluginsdk/examples/ASTRewritePlugin.c`](../../pluginsdk/examples/ASTRewritePlugin.c)
shows the whole cycle including parser interception.

## Lifecycle events

Instead of polling, subscribe to the eleven points where the front end
publishes a declaration:

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

The kinds are `TREE_INITIALIZE`, `SEMA_BEGIN`, `TOP_LEVEL_DECL`,
`INLINE_FUNCTION_DEFINITION`, `INTERESTING_DECL`, `TAG_DEFINITION`,
`TAG_REQUIRED_DEFINITION`, `TENTATIVE_DEFINITION`, `EXTERNAL_DECLARATION`,
`TRANSLATION_UNIT`, and `SEMA_END`;
`NEVERC_AST_LIFECYCLE_EVENT_MASK_ALL` covers all eleven. The event carries
the translation unit, a single declaration, and a declaration array — all
read-only and borrowed for the callback.

## Parser extension

A parser extension gets a token cursor with speculative parsing built in:

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
  /* … build a node … */
  Parser->CursorCommit(Parser->Context, Task, In.Cursor, Checkpoint);
  Out.Disposition = NEVERC_PARSER_EXTENSION_HANDLED;
  Out.ResultKind  = NEVERC_PARSER_RESULT_DECL;
  Out.Node        = MyNode;
}
Parser->CreateExtensionOutput(Parser->Context, Frame, Continuation, &Out,
                              &Output);
```

`ExpectedResult` on the input tells you what the parser needs:
`NEVERC_PARSER_RESULT_DECL`, `STMT`, `EXPR`, `TYPE`, or `ATTRIBUTE`.
`CreateParsedAttribute` builds an attribute in GNU (`__attribute__`), C23
(`[[…]]`), or `__declspec` form.

A provider for `neverc.syntax.parse` itself publishes a whole AST unit:

```c
NevercParserASTUnitDescriptor Unit = {0};
Unit.Header          = /* … */;
Unit.Product         = (NevercInterfaceID){NEVERC_AST_PRODUCT_STANDARD_HIGH,
                                           NEVERC_AST_PRODUCT_STANDARD_LOW};
Unit.TranslationUnit = TU;
Parser->CreateASTUnit(Parser->Context, Frame, &Unit, &Output);
```

`GetASTUnitInfo` reports the unit's `SemanticState`. A unit published as
`NEVERC_AST_UNIT_UNANALYZED` will be replayed through semantic analysis;
`NEVERC_AST_UNIT_SEMANTICALLY_ANALYZED` asserts the provider already did that
work.

## Semantic queries

```c
NevercSemaLookupRequest Request = {0};
Request.Header = /* … */;
Request.Scope  = Scope;
Request.Name   = SV("my_symbol");
Request.Kind   = NEVERC_SEMA_LOOKUP_ORDINARY;   /* or _TAG, _MEMBER */

NevercLookupResultHandle Result;
Sema->LookupName(Sema->Context, Task, &Request, &Result);

NevercSemaLookupResultInfo Info = {0};
Info.Header = /* … */;
Sema->GetLookupResultInfo(Sema->Context, Task, Result, &Info);
/* Info.Kind is NOT_FOUND, FOUND, or AMBIGUOUS; Info.CandidateCount follows. */

for (uint64_t I = 0; I != Info.CandidateCount; ++I) {
  NevercDeclHandle Candidate;
  Sema->GetLookupCandidate(Sema->Context, Task, Result, I, &Candidate);
}
Sema->DestroyLookupResult(Sema->Context, Task, Result);
```

`GetCurrentScope`, `GetScopeInfo`, and `GetScopeDeclaration` walk the scope
chain; scope flags are `NEVERC_SEMA_SCOPE_FILE`, `FUNCTION`, `RECORD`, and
`BLOCK`.

Constant evaluation returns a handle whose info describes the value's shape:

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

Conversions are classified before they are applied, so a plugin can inspect
the decision:

```c
NevercConversionSequenceHandle Sequence;
Sema->ClassifyImplicitConversion(Sema->Context, Task, SourceType, DestType,
                                 &Sequence);
NevercSemaConversionSequenceInfo SeqInfo = {0};
SeqInfo.Header = /* … */;
Sema->GetConversionSequenceInfo(Sema->Context, Task, Sequence, &SeqInfo);
/* SeqInfo.Kind ranges over COMPATIBLE, POINTER_TO_INTEGER,
   INTEGER_TO_POINTER, INCOMPATIBLE_POINTER, DISCARDS_QUALIFIERS,
   ADDRESS_SPACE_MISMATCH, VECTOR, INCOMPATIBLE, and more;
   SeqInfo.Viable and .RequiresDiagnostic follow. */
```

`AreTypesCompatible`, `GetCanonicalType`, `GetTagType`, and `GetBuiltinInfo`
round out the read-only surface.

## The mutation lease

Anything that changes semantic state — creating a type, applying a
conversion, emitting a semantic diagnostic — needs a lease. The lease is what
makes concurrent semantic work safe:

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
`CreateVectorType`, `CreateExplicitCast`, and `EmitDiagnostic` all take the
lease. Conversion contexts are `NEVERC_SEMA_CONVERSION_ASSIGNMENT`,
`ARGUMENT`, `RETURN`, `INITIALIZATION`, and `EXPLICIT_CAST`.

## Semantic extension phases

Each extension phase has a matching input/output pair. The expression hook,
for example:

```c
NevercSemaExpressionExtensionInput In = {0};
In.Header = /* … */;
Sema->GetExpressionExtensionInput(Sema->Context, Frame, Frame->Input, &In);
/* In.Left, In.Right, In.OperatorLocation */

NevercSemaExpressionExtensionOutput Out = {0};
Out.Header      = In.Header;
Out.Disposition = NEVERC_SEMA_EXTENSION_HANDLED;   /* or _UNHANDLED */
Out.Expression  = Result;
Sema->CreateExpressionExtensionOutput(Sema->Context, Frame, Continuation,
                                      &Out, &Output);
```

The same shape applies to `Statement`, `Declaration`, `Type`, `Lookup`, and
`Conversion`. Returning `NEVERC_SEMA_EXTENSION_UNHANDLED` lets the built-in
behaviour run.

A provider for `neverc.sema.analyze` publishes the semantic unit:

```c
NevercSemanticUnitDescriptor Unit = {0};
Unit.Header           = /* … */;
Unit.Product          = (NevercInterfaceID){NEVERC_SEMANTIC_PRODUCT_STANDARD_HIGH,
                                            NEVERC_SEMANTIC_PRODUCT_STANDARD_LOW};
Unit.TranslationUnit  = TU;
Unit.SemanticComplete = NEVERC_TRUE;
Sema->CreateSemanticUnit(Sema->Context, Frame, &Unit, &Output);
```

`GetSemanticUnitInfo` reports `DiagnosticState`
(`NEVERC_SEMANTIC_DIAGNOSTICS_CLEAN` or `_HAS_ERROR`), whether the unit was
replayed, and a verifier summary.

## Rules

- AST and type handles are task-scoped. Never store one past the callback.
- Every builder, mutation, lookup result, conversion sequence, and constant
  value has a matching `Destroy*`; call it on the error path too.
- A semantic mutation without a lease returns `NEVERC_STATUS_INVALID_STATE`.
- Do not mutate the tree from a lifecycle observer — observers are read-only.
  Use an interceptor on the corresponding phase.
- Property IDs and child-slot IDs are schema constants. Do not hard-code
  numeric literals; use the names from `PluginASTSchema.inc` so a schema
  revision is a compile error.
- Check `NevercTypeInfo.Flags` for `HAS_KNOWN_LAYOUT` before trusting
  `SizeInBits` or `AlignmentInBits`.

See `PluginAST.h`, `PluginSema.h`, and `Schema/ASTSchema.json` for the
normative declarations, and `pluginsdk/examples/ASTRewritePlugin.c` for a
working parser interception and atomic tree rewrite.
