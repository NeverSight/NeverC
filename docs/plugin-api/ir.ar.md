<div dir="rtl">

**اللغات**: [English](ir.md) | [简体中文](ir.zh-CN.md) | [繁體中文](ir.zh-TW.md) | [日本語](ir.ja.md) | [한국어](ir.ko.md) | [Français](ir.fr.md) | [Deutsch](ir.de.md) | [Español](ir.es.md) | [Italiano](ir.it.md) | [Русский](ir.ru.md) | [العربية](ir.ar.md)

[← واجهة NeverC الثنائية للإضافات](README.ar.md)

# واجهة التمثيل الوسيط لإضافات NeverC

تكشف [`PluginIR.h`] تمثيلَ LLVM الوسيط عبر ستة جداول قدرات ومخطَّطٍ مولَّد. تستطيع
الإضافة أن تقرأ التمثيل الوسيط وتعيد كتابته، وأن تسجّل تمريرات في خمس نقاط مستقرة
من خط الأنابيب، وأن تعرّف تحليلاتها الخاصة، أو أن تستبدل توليدَ التمثيل الوسيط
وخط أنابيب التحسين بالكامل — دون أن تضمّ ملفًا ترويسيًا واحدًا من LLVM.

رموز العمليات وأنواع الأنماط وخصائص التعليمات كلها **معرّفات مخطَّط مستقرة**، لا
قيم تعدادية من LLVM. وهذه الوساطة هي ما يجعل إضافةً تُترجم اليوم تظل عاملةً حين
ينتقل المضيف إلى إصدار LLVM جديد.

## الواجهات

```c
#include "neverc/Plugin/PluginIR.h"
```

| الواجهة | الجدول | الفتحات | الغرض |
|---|---|--:|---|
| `NEVERC_INTERFACE_IR_CORE_{HIGH,LOW}` | `NevercIRCoreAPI` | 99 | قراءة وتحرير الوحدات والقيم والأنواع والثوابت والبيانات الوصفية والسمات |
| `NEVERC_INTERFACE_IR_BUILDER_{HIGH,LOW}` | `NevercIRBuilderAPI` | 29 | البناء المعامَلاتي |
| `NEVERC_INTERFACE_IR_ANALYSIS_{HIGH,LOW}` | `NevercIRAnalysisAPI` | 13 | التحليلات المدمجة وتحليلات الإضافات |
| `NEVERC_INTERFACE_IR_PASS_{HIGH,LOW}` | `NevercIRPassAPI` | 1 | `RegisterPass` |
| `NEVERC_INTERFACE_IR_GEN_{HIGH,LOW}` | `NevercIRGenAPI` | 5 | استبدال الخفض من SemanticUnit إلى التمثيل الوسيط |
| `NEVERC_INTERFACE_IR_OPTIMIZATION_{HIGH,LOW}` | `NevercIROptimizationAPI` | 7 | استبدال خط أنابيب التحسين بأسره |

كلٌّ منها `NEVERC_INTERFACE_STABLE` عند الرقم الرئيسي 1. تفاوض باستخدام
`NEVERC_IR_*_API_MAJOR` / `_MINOR` المقابلة، وتحقّق أن `TableSize` يبلغ آخر فتحة
تستدعيها، تمامًا كما يفعل [`pluginsdk/examples/FunctionPass.c`]:

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

## المراحل

ثماني مراحل للتمثيل الوسيط:

| المرحلة | السياسة |
|---|---|
| `neverc.ir.generate` | OBSERVABLE، INTERCEPTABLE، REPLACEABLE |
| `neverc.ir.optimize` | OBSERVABLE، INTERCEPTABLE، REPLACEABLE |
| `neverc.ir.pass.pre_opt` | OBSERVABLE، INTERCEPTABLE |
| `neverc.ir.pass.pipeline_start` | OBSERVABLE، INTERCEPTABLE |
| `neverc.ir.pass.optimizer_last` | OBSERVABLE، INTERCEPTABLE |
| `neverc.ir.pass.post_opt` | OBSERVABLE، INTERCEPTABLE |
| `neverc.ir.pass.pre_codegen` | OBSERVABLE، INTERCEPTABLE |
| `neverc.ir.final_verify` | OBSERVABLE، **بوابة مضيف مختومة** |

مراحل `pass.*` الخمس هي ما يشير إليه `NevercIRPassDescriptor.Phase`. وتشغّل
`neverc.ir.final_verify` مدقّقَ LLVM، ولا يستطيع أي شيء اعتراضها أو استبدالها أو
تخطّيها — بما في ذلك مزوّد التحسين.

## المخطَّط

[`Schema/PluginIRSchema.inc`] مولَّد ويضمّه [`PluginIR.h`]. وهو ينشر بصمةً ومجموعات
الثوابت التالية:

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

تُوسَم المعرّفات بمجالها في البايت الأعلى — `0x41……` للأنواع، و`0x42……` لأنواع
القيم، و`0x43……` لرموز العمليات، و`0x49……` للخصائص — فالقيمة المستعملة في غير
موضعها تُرفض بدل أن تُقرأ خطأً.

## المقابض والملكية

مقابض التمثيل الوسيط أزواج `{Owner, Value}` معتِمة نطاقها مهمة واحدة، وكل ما
وراءها مملوك للمضيف.

- لا تحتفظ بمقبض بعد انتهاء ردّ ندائه أو مهمته.
- لا تستعمل مقبضًا في جلسة أو مهمة أخرى.
- الاستبدال المودَع يُبطل مقابض الأشياء التي استُبدلت.
- التعديل المُجهَض يجعل المقابض التي أنشأها بائدة.
- الأخطاء هي `NEVERC_STATUS_STALE_HANDLE` أو `WRONG_SCOPE` أو `WRONG_TYPE` —
  ولن يكون الناتج أبدًا مؤشر LLVM خامًا.

السلاسل ورؤى البايتات العائدة من استعلامٍ ما مستعارة طوال ردّ النداء. والاستثناء
الوحيد هو `ExportModule`، فهو يعيد `NevercIRSerializedBufferHandle` يجب أن
تعيده إلى `ReleaseSerializedBuffer`.

## اجتياز وحدة

تُقرأ المجموعات عبر مؤشر يحمل جيلَه الخاص، فيُكتشف أي تعديل يقع في منتصف
الاجتياز بدل أن تُتخطّى المدخلات صمتًا:

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

كرّر حتى يعود `Count` صفرًا. والمجموعات السبع هي `MODULE_FUNCTIONS`
و`MODULE_GLOBALS` و`MODULE_ALIASES` و`MODULE_I_FUNCS` و`FUNCTION_ARGUMENTS`
و`FUNCTION_BLOCKS` و`BLOCK_INSTRUCTIONS`.

وما عدا ذلك استعلامٌ مباشر: `GetValueKind` و`GetValueType`
و`GetOperandCount` / `GetOperand` / `SetOperand` و`GetValueUseCount` /
`GetValueUse` و`GetTerminator` و`GetPredecessor*` و`GetSuccessor*`
و`GetPHIIncoming*`، وعلى مستوى الوحدة `GetModuleIdentifier`
و`GetModuleTargetTriple` و`GetModuleDataLayout` و`GetModuleInlineAssembly` مع
ضوابطها.

## الأنواع والثوابت

الأنواع مُدمَجة داخليًا، فالسؤال مرتين يعطي المقبض نفسه:

```c
NevercIRTypeHandle I32, Ptr, Fn;
Core->GetIntegerType(Core->Context, Task, 32, &I32);
Core->GetPointerType(Core->Context, Task, /*AddressSpace=*/0, &Ptr);

NevercIRTypeHandle Params[] = {I32, Ptr};
Core->GetFunctionType(Core->Context, Task, I32, Params, 2,
                      /*Variadic=*/0, &Fn);
```

تأخذ `GetPrimitiveType` نوعَ مخطَّط مثل `NEVERC_IR_TYPE_VOID` أو `_FLOAT` أو
`_DOUBLE` أو `_TOKEN`، بينما تغطي البقيةَ `GetArrayType` و`GetVectorType` (مع
راية `Scalable`) و`GetStructType` (مسمّاة أو حرفية، محزومة أو لا).

تُبنى الثوابت الصحيحة والعائمة من كلمات 64 بتًا صغيرة النهاية، فلا يحتاج `i128`
مسارًا خاصًا:

```c
uint64_t Words[2] = {0xFFFFFFFFFFFFFFFFULL, 0x1ULL};
NevercIRValueHandle C;
Core->CreateIntegerConstant(Core->Context, Task, I128, Words, 2, &C);
```

وتغطي `GetNullConstant` و`GetPoisonConstant` و`GetUndefConstant`
و`CreateAggregateConstant` و`GetGlobalAddressConstant` الحالاتِ البسيطة، بينما
تبني `CreateConstantBinaryExpression` و`CreateConstantCastExpression`
و`CreateConstantCompareExpression` و`CreateConstantGEPExpression` التعابيرَ
الثابتة.

## خصائص التعليمات

بدل موصِّل لكل راية، تمرّ تفاصيل التعليمة عبر قيمة خاصية موسومة مفتاحُها معرّف
المخطَّط:

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

والخصائص الـ23 هي `NAME` و`FAST_MATH_FLAGS` و`NUW` و`NSW` و`EXACT`
و`DISJOINT` و`VOLATILE` و`ALIGNMENT` و`ATOMIC_ORDERING` و`SYNC_SCOPE`
و`PREDICATE` و`CALLING_CONVENTION` و`TAIL_CALL_KIND` و`INDICES` و`WEAK`
و`SUCCESS_ORDERING` و`FAILURE_ORDERING` و`INBOUNDS` و`SOURCE_ELEMENT_TYPE`
و`ALLOCATED_TYPE` و`ATTRIBUTES` و`CLEANUP` و`NUSW`. وتمتد الترتيبات الذرّية من
`NOT_ATOMIC` إلى `SEQUENTIALLY_CONSISTENT`، وأنواع نداء الذيل هي `NONE`
و`TAIL` و`MUST_TAIL` و`NO_TAIL`، ورايات fast-math هي البتات السبع المعتادة من
`ALLOW_REASSOC` إلى `APPROX_FUNC`.

## السمات

السمات قيمٌ تنشئها ثم تُرفقها، وهذا ما يجعل الأنواع الأربعة (`ENUM` و`INTEGER`
و`STRING` و`TYPE`) متسقة:

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

ويستعمل [`pluginsdk/examples/CustomCallConvPlugin.c`] هذا مع
`GetFunctionStringAttribute` لقيادة اصطلاح نداءٍ معرَّف بالبيانات.

## التعديل المعامَلاتي

يمرّ كل تغيير بنيوي عبر `NevercIRBuilderAPI`. التعديل هو المعامَلة، والباني مؤشر
داخلها.

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

النطاقات هي `NEVERC_IR_MUTATION_SCOPE_MODULE` و`_FUNCTION` و`_LOOP`، ويسمّي
`ScopeRoot` الدالةَ أو ترويسةَ الحلقة. والإيداع يتحقق من المرشَّح وينشر ذريًّا —
فإذا أخفق المدقّق تراجع المضيفُ وبقيت الوحدة السابقة دون مساس.

ونداءات البناء هي `BuildBinary` و`BuildUnary` و`BuildCompare` و`BuildCast`
و`BuildSelect` و`BuildAlloca` و`BuildLoad` و`BuildStore`
و`BuildGetElementPtr` و`BuildCall` و`BuildPhi` و`BuildBranch`
و`BuildConditionalBranch` و`BuildUnreachable` و`BuildReturn`
و`BuildReturnVoid`. وتنطبق `SetDebugLocation` و`SetFastMathFlags` على كل ما
يصدره الباني بعدهما.

لاحظ عدم التناظر: تأخذ `AddPhiIncoming` و`CreateFunction` و`CreateBasicBlock`
**التعديلَ** لا الباني، لأنها غير مرتبطة بموضع إدراج.

و`DestroyMutation` منفصل عن الإيداع والإجهاض. فكل `BeginMutation` يحتاج
`DestroyMutation` واحدًا بالضبط، أيًّا كانت النهاية التي بلغتها المعامَلة.

## التمريرات

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

المستويات هي `MODULE` و`CGSCC` و`FUNCTION` و`LOOP`. ولا يحمل النداء إلا المقابض
الصالحة لمستواه:

```c
typedef struct NevercIRPassInvocation {
  NevercABITableHeader Header;
  NevercTaskHandle Task;
  NevercInterfaceID Phase;
  NevercStringView PassID;
  NevercIRPassLevel Level;
  NevercIROptimizationLevel OptimizationLevel;  /* O0…O3, Os, Oz */
  NevercIRModuleHandle Module;
  NevercIRValueHandle Function;                 /* FUNCTION و LOOP       */
  NevercIRValueHandle LoopHeader;               /* LOOP فقط              */
  const NevercIRValueHandle *SCCFunctions;      /* CGSCC فقط             */
  uint64_t SCCFunctionCount;
  const NevercIRCoreAPI *Core;
  const NevercIRBuilderAPI *Builder;
  const NevercIRAnalysisAPI *Analyses;
  uint64_t Reserved[2];
} NevercIRPassInvocation;
```

تأتي مؤشرات الواجهات الثلاثة مع النداء، فلا يحتاج جسمُ التمريرة جدولًا مخزّنًا.

وأبلغ عمّا نجا عبر `OutPreserved`:

```c
OutPreserved->Flags = NEVERC_IR_PRESERVE_ALL;   /* أو _NONE، أو _CFG */
```

`NEVERC_IR_PRESERVE_CFG` يعني أن مخطط تدفق التحكم سليم وإن تغيّرت التعليمات.
وتُحفظ التحليلات المخصّصة بإدراجها في `CustomAnalyses`. ولا تدّعِ
`PRESERVE_ALL` بعد تغيير التمثيل الوسيط — فالمهايئ يقارن جيلَ الوحدة ويرفض
الادّعاء الكاذب.

وقد تعمل تمريرات الدوال والحلقات على التوازي، فيجب أن توافق حالةُ الإضافة
القابلة للتغيير نموذجَ `NevercConcurrencyModel` الذي أعلنته الإضافة.

## التحليلات

سبعة تحليلات مدمجة قابلة للاستعلام بالمعرّف: `DOMINATOR_TREE`
و`POST_DOMINATOR_TREE` و`LOOP_INFO` و`SCALAR_EVOLUTION` و`MEMORY_SSA`
و`CALL_GRAPH` و`ALIAS`.

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

ولكلٍّ منها موصِّلات مُنمّطة بدل كتلة معتِمة: `DominatorTreeDominates`
و`GetLoopCount` / `GetLoopHeader` / `GetLoopForBlock`
و`GetScalarEvolutionConstantTripCount` و`GetMemoryAccessKind` (`NONE` و`USE`
و`DEF` و`PHI` و`LIVE_ON_ENTRY`) و`GetDirectCalleeCount` / `GetDirectCallee`
و`Alias` (`NO` و`MAY` و`PARTIAL` و`MUST`).

ويُسجَّل تحليل الإضافة بدورة حياته الخاصة:

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

ويُخبَر `Invalidate` بالسبب — `INVALIDATED_BY_PASS` أو
`INVALIDATED_BY_PLAN_DESTROY`. وتُخزَّن النتائج مؤقتًا لكل نداء وتُسقَط بحسب ما
حفظته التمريرةُ الجارية. وتُرفض حلقاتُ التبعية عند التسجيل، كما يُرفض تعديل
التمثيل الوسيط من داخل ردّ نداء تحليل.

## استبدال التوليد والتحسين

تستبدل `NevercIRGenAPI` المرحلةَ `neverc.ir.generate`:

```c
NevercIRGeneratePhaseInput In = {0};
In.Header = /* … */;
Gen->GetGeneratePhaseInput(Gen->Context, Frame, Frame->Input, &In);
/* In.SemanticUnit و.TargetTriple و.DataLayout و.SourceIdentity و
   .SourceDigest */

const NevercIRCoreAPI *Core;
const NevercIRBuilderAPI *Builders;
Gen->CreateModule(Gen->Context, Frame, SV("my.module"), &Core, &Builders);
/* … ابنِ الوحدة … */

NevercIRModuleArtifactDescriptor Descriptor = {0};
Descriptor.Header           = /* … */;
Descriptor.Product          = MyProductID;
Descriptor.DependencyDigest = Digest;
Gen->PublishModule(Gen->Context, Frame, &Descriptor, &Output);
```

وتبدأ `ImportModule` من bitcode أو تمثيل وسيط نصّي بدل وحدة فارغة. ولـ
`NevercIROptimizationAPI` الشكلُ نفسه للمرحلة `neverc.ir.optimize`، مضافًا
إليه `GetInputModule` للوصول إلى الوحدة الداخلة، و`RunBuiltinPipeline` لتفويض
خط الأنابيب المدمج ثم معالجة نتيجته لاحقًا.

كلا المسارين ينشران عبر المضيف بدل إعادة مؤشر، ويتحققان من توافق الهدف،
ويحفظان الوحدةَ القديمة ذريًّا إن أخفق النشر. وتظل `neverc.ir.final_verify`
تعمل بعد ذلك.

## أمثلة

| الملف | ما يعرضه |
|---|---|
| [`pluginsdk/examples/FunctionPass.c`] | تمريرة دالة للقراءة فقط، مع مفاوضة ABI |
| [`pluginsdk/examples/ExamplePlugin.c`] | تمريرة على مستوى الوحدة تجتاز الدوال بمؤشر قيم |
| [`pluginsdk/examples/CustomCallConvPlugin.c`] | السمات وخصائص موضع النداء |

```sh
cmake --build build-neverc --target neverc-plugin-example-function-pass
build-neverc/bin/neverc \
  -fplugin=build-neverc/neverc/pluginsdk/examples/host/FunctionPass.so \
  -O2 -c input.c -o input.o
```

استعمل لاحقة الوحدة التي أنتجها CMake لمنصّتك.

## القواعد

- أعِد `NevercStatus` من كل ردّ نداء. فإخفاق الإضافة يصير تشخيصًا مُهيكلًا، ولا
  تدع استثناءً يعبر حدّ لغة C أبدًا.
- صفّر كل بنية خرج واضبط `Header` فيها قبل النداء الذي يملؤها.
- لا تكتب أعدادًا حرفية لرموز العمليات أو الأنواع أو الخصائص. استعمل أسماء
  [`PluginIRSchema.inc`] كي تصير مراجعةُ المخطَّط خطأ ترجمة.
- كل `BeginMutation` يبلغ `DestroyMutation` واحدًا بالضبط، وكل `CreateBuilder`
  يبلغ `DestroyBuilder` واحدًا بالضبط، على مسارات الخطأ أيضًا.
- حرّر ما تسلّمه من `ExportModule` بـ`ReleaseSerializedBuffer`.
- لا تدّعِ `NEVERC_IR_PRESERVE_ALL` بعد تعديل التمثيل الوسيط.
- افترض أن تمريرات الدوال والحلقات تعمل على التوازي، ما لم تعلن الإضافة
  `NEVERC_CONCURRENCY_SESSION_SERIAL`.
- `neverc.ir.final_verify` مختومة. ولا شيء تفعله إضافةٌ يمكنه تخطّيها.

انظر [`PluginIR.h`] و[`Schema/PluginIRSchema.inc`] و[`Schema/PhaseSchema.json`]
و[`coverage.json`] للتصريحات المعيارية وثوابت المخطَّط وسياسات المراحل وأدلة
الاختبارات.

<!-- reference links -->
[`coverage.json`]: coverage.json
[`PluginIR.h`]: ../../neverc/include/neverc/Plugin/PluginIR.h
[`PluginIRSchema.inc`]: ../../neverc/include/neverc/Plugin/Schema/PluginIRSchema.inc
[`pluginsdk/examples/CustomCallConvPlugin.c`]: ../../pluginsdk/examples/CustomCallConvPlugin.c
[`pluginsdk/examples/ExamplePlugin.c`]: ../../pluginsdk/examples/ExamplePlugin.c
[`pluginsdk/examples/FunctionPass.c`]: ../../pluginsdk/examples/FunctionPass.c
[`Schema/PhaseSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PhaseSchema.json
[`Schema/PluginIRSchema.inc`]: ../../neverc/include/neverc/Plugin/Schema/PluginIRSchema.inc

</div>
