**اللغات**: [English](ast-sema.md) | [简体中文](ast-sema.zh-CN.md) | [繁體中文](ast-sema.zh-TW.md) | [日本語](ast-sema.ja.md) | [한국어](ast-sema.ko.md) | [Français](ast-sema.fr.md) | [Deutsch](ast-sema.de.md) | [Español](ast-sema.es.md) | [Italiano](ast-sema.it.md) | [Русский](ast-sema.ru.md) | [العربية](ast-sema.ar.md)

[← واجهة NeverC الثنائية للإضافات](README.ar.md)

# واجهة الشجرة النحوية والدلالات لإضافات NeverC

ثلاثة جداول تغطي الواجهة الأمامية. تتيح `NevercParserAPI` للإضافة أن تتولى جزءًا
من التحليل النحوي — شكل تصريح جديد، أو جملة جديدة — عبر قيادة مؤشر رموز مزوَّد
بنقاط تفتيش. وتقرأ `NevercASTAPI` الشجرة وتعدّلها بأسلوب معامَلاتي. أما
`NevercSemaAPI` فتتولى البحث عن الأسماء، وبناء الأنواع، وتصنيف التحويلات، وتقييم
الثوابت.

تُكشف الشجرة عبر **مخطط (schema)**، لا عبر مرآة بلغة C لتسلسل أصناف Clang.
فالعُقد مقابض معتِمة؛ تطلب خاصيةً بمعرّف مستقر فتحصل على `NevercASTValue` موسومة.
وهذه الوساطة هي ما يبقي السطح مستقرًا عبر إصدارات LLVM.

## الواجهات

```c
#include "neverc/Plugin/PluginAST.h"
#include "neverc/Plugin/PluginSema.h"
```

| الواجهة | الجدول | وحدات ماكرو الإصدار |
|---|---|---|
| `NEVERC_INTERFACE_AST_{HIGH,LOW}` | `NevercASTAPI` | `NEVERC_AST_API_MAJOR` (1) / `_MINOR` (1) |
| `NEVERC_INTERFACE_PARSER_{HIGH,LOW}` | `NevercParserAPI` | `NEVERC_PARSER_API_MAJOR` / `_MINOR` |
| `NEVERC_INTERFACE_SEMA_{HIGH,LOW}` | `NevercSemaAPI` | `NEVERC_SEMA_API_MAJOR` / `_MINOR` |

يوفّر `Schema/PluginASTSchema.inc` معرّفات أنواع العُقد والخصائص وفتحات الأبناء،
ويجب أن يساوي رقمه الرئيسي للقدرات `NEVERC_AST_API_MAJOR`.

## المراحل

سبع مراحل نحوية وسبع مراحل دلالية، وكلها
`OBSERVABLE | INTERCEPTABLE | REPLACEABLE`:

| النحو | الدلالة |
|---|---|
| `neverc.syntax.parse` | `neverc.sema.analyze` |
| `neverc.syntax.extension.declaration` | `neverc.sema.extension.declaration` |
| `neverc.syntax.extension.statement` | `neverc.sema.extension.statement` |
| `neverc.syntax.extension.expression` | `neverc.sema.extension.expression` |
| `neverc.syntax.extension.type_name` | `neverc.sema.extension.type` |
| `neverc.syntax.extension.attribute` | `neverc.sema.extension.lookup` |
| `neverc.syntax.extension.keyword` | `neverc.sema.extension.conversion` |

تستهلك `neverc.syntax.parse` دفقَ رموز وتنتج وحدة شجرة، وتستهلك
`neverc.sema.analyze` تلك الوحدة وتنتج وحدةً دلالية. ومراحل `extension.*` هي
خطاطيف امتدادات اللغة: يسأل المضيف إن كانت إضافةٌ ما ترغب في معالجة هذا التركيب
قبل أن يرتدّ إلى السلوك المدمج.

## نموذج المخطط

كل عقدة هي `NevercASTNodeHandle`، ولها أسماء مرادفة مُنمّطة
(`NevercDeclHandle` و`NevercStmtHandle` و`NevercExprHandle` و`NevercTypeHandle`
و`NevercAttrHandle` و`NevercDeclContextHandle` و`NevercTypeLocHandle`).
والتنقّل البنيوي موحّد:

```c
NevercASTNodeInfo Info = {0};
Info.Header = (NevercABITableHeader){sizeof(Info), NEVERC_AST_API_MAJOR,
                                     NEVERC_AST_API_MINOR, 0};
AST->GetNodeInfo(AST->Context, Task, Node, &Info);
/* Info.Kind و.Domain و.Parent و.DeclContext و.SourceRange */

uint64_t ChildCount = 0;
AST->GetChildCount(AST->Context, Task, Node, &ChildCount);
for (uint64_t I = 0; I != ChildCount; ++I) {
  NevercASTNodeHandle Child;
  AST->GetChild(AST->Context, Task, Node, I, &Child);
}
```

و`Domain` واحد من `NEVERC_AST_SCHEMA_DOMAIN_DECL` أو `STMT` أو `TYPE` أو
`TYPE_LOC` أو `ATTR`.

وتُقرأ الخصائص بالمعرّف داخل قيمة موسومة:

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

يختار `Type` أي عضو هو الحيّ: `NEVERC_AST_VALUE_BOOL` و`I64` و`U64` و`STRING`
و`SOURCE_RANGE` و`NODE` و`DECL` و`STMT` و`EXPR` و`TYPE` و`TYPE_LOC` و`ATTR`
و`IDENTIFIER` و`ENUM` و`VERSION` و`PARAMETER_INDEX` و`ALIGNMENT_OPERAND`.
ويسجّل المخطط لكل خاصية نمطَ وصولها (`READ_ONLY` و`READ_WRITE` و`BUILD_ONLY`)
وعدديّتها (`REQUIRED` و`OPTIONAL` و`MANY`)، فمحاولة الكتابة في خاصية للقراءة فقط
تفشل عند الواجهة بدل أن تُفسد الشجرة.

واجتياز عُقد كثيرة دفعةً واحدة أرخص عبر النداءات الدُفعية، فهي تأخذ خطوةَ خرج
تتيح لك الكتابة مباشرةً في مصفوفة بناك أنت:

```c
AST->GetNodeInfoBatch(AST->Context, Task, Nodes, NodeCount,
                      OutInfos, OutInfoCapacity, OutInfoStride);
AST->GetPropertyBatch(AST->Context, Task, Nodes, Properties, QueryCount,
                      OutValues, OutValueCapacity, OutValueStride);
```

## الموصِّلات المُنمّطة

للتراكيب التي تلمسها الإضافات أكثر من غيرها قارئات مباشرة بدل البحث عن الخصائص:

| النداء | ما تعطيه |
|---|---|
| `GetTranslationUnit` | التصريح الجذر |
| `GetFunctionDeclInfo`، `GetFunctionDeclParameter` | الاسم والنوع ونوع الإرجاع والجسم وعدد المعاملات والتغيّرية والتعريف |
| `GetVarDeclInfo` | الاسم والنوع والمُهيّئ والتعريف والتخزين العام |
| `GetRecordDeclInfo` | الاسم وعدد الحقول والاكتمال وكونه اتحادًا وعضو المصفوفة المرنة |
| `GetDeclAttributeCount`، `GetDeclAttribute`، `GetAttrInfo` | نوع السمة وكتابتها وضِمنيّتها ووراثتها |
| `GetDeclRefExprInfo` | التصريح المُشار إليه والمُكتشَف، والنوع |
| `GetCallExprInfo`، `GetCallExprArgument` | المُستدعَى، والمستدعَى المباشر، والنوع، والوسائط |
| `GetBinaryOperatorInfo` | اليسار واليمين والنوع وكتابة المعامل ونوعه |
| `GetCompoundStmtInfo` | عدد الجمل |
| `GetIntegerLiteralInfo`، `GetIntegerLiteralWord` | عرض البتات والكلمات صغيرة النهاية |
| `GetTypeInfo`، `GetTypeElement` | وصف النوع كاملًا |
| `GetBuiltinType` | نوع مدمج بحسب `NevercBuiltinTypeKind` |

وأغنى هذه البنى هي `NevercTypeInfo`:

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

تمتد أنواع البنى المدمجة من `NEVERC_BUILTIN_TYPE_VOID` و`_BOOL` صعودًا في سُلّم
الأعداد الصحيحة حتى `_LONG_DOUBLE`، وأنواع المعاملات الثنائية من
`NEVERC_BINARY_OPERATOR_MUL` حتى `_COMMA`.

## البناء والتعديل

البناء يستعمل بانيًا، والتعديل يستعمل معامَلة. وهما يتركّبان: ابنِ عقدة الاستبدال
أولًا، ثم بدّلها في مكانها.

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

تأخذ `ASTBuilderSetIntegerValue` قيمةَ `NevercAPIntView` (كلمات صغيرة النهاية مع
عرض البتات) للحرفيات الأعرض من 64 بتًا، وتضبط
`ASTBuilderSetBinaryOperatorKind` معاملَ التعبير الثنائي.

```c
NevercASTMutationHandle Mutation;
AST->BeginASTMutation(AST->Context, Task, &Mutation);
AST->ASTMutationReplaceChild(AST->Context, Task, Mutation, Parent, SlotID,
                             Index, NewNode);
AST->CommitASTMutation(AST->Context, Task, Mutation);   /* أو AbortASTMutation */
AST->DestroyASTMutation(AST->Context, Task, Mutation);
```

يتحقق الإيداع من الشجرة المُهيّأة وينشرها ذريًّا. والإيداع الفاشل يترك الشجرة
السابقة سليمة، والإجهاض يجعل المقابض التي أنشأها ذلك التعديل بائدة. ويعرض
[`pluginsdk/examples/ASTRewritePlugin.c`](../../pluginsdk/examples/ASTRewritePlugin.c)
الدورة كاملة بما فيها اعتراض المحلل النحوي.

## أحداث دورة الحياة

بدل الاستطلاع المتكرر، اشترك في المواضع الأحد عشر التي تنشر فيها الواجهة الأمامية
تصريحًا:

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

والأنواع هي `TREE_INITIALIZE` و`SEMA_BEGIN` و`TOP_LEVEL_DECL`
و`INLINE_FUNCTION_DEFINITION` و`INTERESTING_DECL` و`TAG_DEFINITION`
و`TAG_REQUIRED_DEFINITION` و`TENTATIVE_DEFINITION` و`EXTERNAL_DECLARATION`
و`TRANSLATION_UNIT` و`SEMA_END`؛ ويغطي
`NEVERC_AST_LIFECYCLE_EVENT_MASK_ALL` الأحد عشر جميعًا. ويحمل الحدث وحدةَ
الترجمة، وتصريحًا مفردًا، ومصفوفةَ تصريحات — كلها للقراءة فقط ومستعارة طوال ردّ
النداء.

## امتداد المحلل النحوي

يحصل امتداد المحلل على مؤشر رموز فيه التحليل التخميني مدمجٌ أصلًا:

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
  /* … ابنِ عقدة … */
  Parser->CursorCommit(Parser->Context, Task, In.Cursor, Checkpoint);
  Out.Disposition = NEVERC_PARSER_EXTENSION_HANDLED;
  Out.ResultKind  = NEVERC_PARSER_RESULT_DECL;
  Out.Node        = MyNode;
}
Parser->CreateExtensionOutput(Parser->Context, Frame, Continuation, &Out,
                              &Output);
```

ويخبرك `ExpectedResult` في الدخل بما يحتاجه المحلل:
`NEVERC_PARSER_RESULT_DECL` أو `STMT` أو `EXPR` أو `TYPE` أو `ATTRIBUTE`.
وتبني `CreateParsedAttribute` سمةً بصيغة GNU (`__attribute__`) أو C23
(`[[…]]`) أو `__declspec`.

أما مزوّد `neverc.syntax.parse` نفسها فينشر وحدة شجرة كاملة:

```c
NevercParserASTUnitDescriptor Unit = {0};
Unit.Header          = /* … */;
Unit.Product         = (NevercInterfaceID){NEVERC_AST_PRODUCT_STANDARD_HIGH,
                                           NEVERC_AST_PRODUCT_STANDARD_LOW};
Unit.TranslationUnit = TU;
Parser->CreateASTUnit(Parser->Context, Frame, &Unit, &Output);
```

ويبلّغ `GetASTUnitInfo` عن `SemanticState` الوحدة. فالوحدة المنشورة بوصفها
`NEVERC_AST_UNIT_UNANALYZED` ستُعاد عبر التحليل الدلالي، بينما
`NEVERC_AST_UNIT_SEMANTICALLY_ANALYZED` تؤكد أن المزوّد أنجز ذلك العمل سلفًا.

## الاستعلامات الدلالية

```c
NevercSemaLookupRequest Request = {0};
Request.Header = /* … */;
Request.Scope  = Scope;
Request.Name   = SV("my_symbol");
Request.Kind   = NEVERC_SEMA_LOOKUP_ORDINARY;   /* أو _TAG أو _MEMBER */

NevercLookupResultHandle Result;
Sema->LookupName(Sema->Context, Task, &Request, &Result);

NevercSemaLookupResultInfo Info = {0};
Info.Header = /* … */;
Sema->GetLookupResultInfo(Sema->Context, Task, Result, &Info);
/* Info.Kind هي NOT_FOUND أو FOUND أو AMBIGUOUS؛ ثم يأتي Info.CandidateCount. */

for (uint64_t I = 0; I != Info.CandidateCount; ++I) {
  NevercDeclHandle Candidate;
  Sema->GetLookupCandidate(Sema->Context, Task, Result, I, &Candidate);
}
Sema->DestroyLookupResult(Sema->Context, Task, Result);
```

وتمشي `GetCurrentScope` و`GetScopeInfo` و`GetScopeDeclaration` على سلسلة
النطاقات؛ ورايات النطاق هي `NEVERC_SEMA_SCOPE_FILE` و`FUNCTION` و`RECORD`
و`BLOCK`.

ويعيد تقييمُ الثوابت مقبضًا تصف معلوماتُه شكلَ القيمة:

```c
NevercConstantValueHandle Value;
Sema->EvaluateConstant(Sema->Context, Task, Expression, &Value);

NevercSemaConstantValueInfo Info = {0};
Info.Header = /* … */;
Sema->GetConstantValueInfo(Sema->Context, Task, Value, &Info);
/* Info.Kind: NONE، INDETERMINATE، INTEGER، FLOAT، FIXED_POINT،
   COMPLEX_INTEGER، COMPLEX_FLOAT، ADDRESS، VECTOR، ARRAY، STRUCT، UNION،
   ADDRESS_LABEL_DIFFERENCE. */

uint64_t Word;
Sema->GetConstantIntegerWord(Sema->Context, Task, Value, 0, &Word);
Sema->DestroyConstantValue(Sema->Context, Task, Value);
```

وتُصنَّف التحويلات قبل تطبيقها، فتستطيع الإضافة أن تفحص القرار:

```c
NevercConversionSequenceHandle Sequence;
Sema->ClassifyImplicitConversion(Sema->Context, Task, SourceType, DestType,
                                 &Sequence);
NevercSemaConversionSequenceInfo SeqInfo = {0};
SeqInfo.Header = /* … */;
Sema->GetConversionSequenceInfo(Sema->Context, Task, Sequence, &SeqInfo);
/* SeqInfo.Kind تمتد على COMPATIBLE، POINTER_TO_INTEGER،
   INTEGER_TO_POINTER، INCOMPATIBLE_POINTER، DISCARDS_QUALIFIERS،
   ADDRESS_SPACE_MISMATCH، VECTOR، INCOMPATIBLE وغيرها؛
   ثم يأتي SeqInfo.Viable و.RequiresDiagnostic. */
```

وتُكمل `AreTypesCompatible` و`GetCanonicalType` و`GetTagType`
و`GetBuiltinInfo` سطحَ القراءة فقط.

## عقد التعديل

كل ما يغيّر الحالة الدلالية — إنشاء نوع، أو تطبيق تحويل، أو إصدار تشخيص دلالي —
يحتاج عقدًا (lease). والعقد هو ما يجعل العمل الدلالي المتزامن آمنًا:

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

وتأخذ `CreateConstantArrayType` و`CreateFunctionType` و`CreateAtomicType`
و`CreateVectorType` و`CreateExplicitCast` و`EmitDiagnostic` هذا العقدَ جميعًا.
وسياقات التحويل هي `NEVERC_SEMA_CONVERSION_ASSIGNMENT` و`ARGUMENT` و`RETURN`
و`INITIALIZATION` و`EXPLICIT_CAST`.

## مراحل الامتداد الدلالي

لكل مرحلة امتداد زوجُ دخل/خرج مقابل. وخطّاف التعبيرات مثالًا:

```c
NevercSemaExpressionExtensionInput In = {0};
In.Header = /* … */;
Sema->GetExpressionExtensionInput(Sema->Context, Frame, Frame->Input, &In);
/* In.Left و In.Right و In.OperatorLocation */

NevercSemaExpressionExtensionOutput Out = {0};
Out.Header      = In.Header;
Out.Disposition = NEVERC_SEMA_EXTENSION_HANDLED;   /* أو _UNHANDLED */
Out.Expression  = Result;
Sema->CreateExpressionExtensionOutput(Sema->Context, Frame, Continuation,
                                      &Out, &Output);
```

وينطبق الشكل نفسه على `Statement` و`Declaration` و`Type` و`Lookup`
و`Conversion`. وإعادة `NEVERC_SEMA_EXTENSION_UNHANDLED` تدع السلوك المدمج يعمل.

ومزوّد `neverc.sema.analyze` ينشر الوحدة الدلالية:

```c
NevercSemanticUnitDescriptor Unit = {0};
Unit.Header           = /* … */;
Unit.Product          = (NevercInterfaceID){NEVERC_SEMANTIC_PRODUCT_STANDARD_HIGH,
                                            NEVERC_SEMANTIC_PRODUCT_STANDARD_LOW};
Unit.TranslationUnit  = TU;
Unit.SemanticComplete = NEVERC_TRUE;
Sema->CreateSemanticUnit(Sema->Context, Frame, &Unit, &Output);
```

ويبلّغ `GetSemanticUnitInfo` عن `DiagnosticState`
(`NEVERC_SEMANTIC_DIAGNOSTICS_CLEAN` أو `_HAS_ERROR`)، وعمّا إذا أُعيدت الوحدة،
وعن ملخّص المدقّق.

## القواعد

- مقابض الشجرة والأنواع نطاقها المهمة. لا تحتفظ بأيٍّ منها بعد ردّ النداء.
- لكل بانٍ وتعديل ونتيجة بحث وسلسلة تحويل وقيمة ثابتة نداءُ `Destroy*` مقابل؛
  استدعِه على مسار الخطأ أيضًا.
- التعديل الدلالي بلا عقد يعيد `NEVERC_STATUS_INVALID_STATE`.
- لا تعدّل الشجرة من مراقب دورة الحياة — فالمراقبون للقراءة فقط. استعمل معترِضًا
  على المرحلة المقابلة.
- معرّفات الخصائص وفتحات الأبناء ثوابتُ مخطط. لا تكتب أعدادًا حرفية مباشرة؛
  استعمل الأسماء من `PluginASTSchema.inc` كي تصبح مراجعةُ المخطط خطأ ترجمة.
- تحقّق من وجود `HAS_KNOWN_LAYOUT` في `NevercTypeInfo.Flags` قبل أن تثق بـ
  `SizeInBits` أو `AlignmentInBits`.

انظر `PluginAST.h` و`PluginSema.h` و`Schema/ASTSchema.json` للتصريحات المعيارية،
و`pluginsdk/examples/ASTRewritePlugin.c` لاعتراض محلل نحوي وإعادة كتابة ذرّية
للشجرة يعملان فعلًا.
