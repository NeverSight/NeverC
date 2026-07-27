<div dir="rtl">

**اللغات**: [English](target-mc-object.md) | [简体中文](target-mc-object.zh-CN.md) | [繁體中文](target-mc-object.zh-TW.md) | [日本語](target-mc-object.ja.md) | [한국어](target-mc-object.ko.md) | [Français](target-mc-object.fr.md) | [Deutsch](target-mc-object.de.md) | [Español](target-mc-object.es.md) | [Italiano](target-mc-object.it.md) | [Русский](target-mc-object.ru.md) | [العربية](target-mc-object.ar.md)

[← واجهة NeverC الثنائية للإضافات](README.ar.md)

# واجهة إضافات NeverC للهدف وMC والتجميع والكائنات

الواجهة الخلفية أربع ترويسات وتسع وعشرون مرحلة. يصف [`PluginTarget.h`] هدفًا
والمسارات المارّة عبر توليد الشيفرة. ويبني [`PluginMC.h`] شيفرة الآلة ويراقبها.
أما تحليل التجميع وطباعته فيسكنان الترويسة نفسها. ويحوّل [`PluginObject.h`] ملفًا
قابلًا لإعادة التموضع إلى رسم بياني مُطبَّع والعكس.

معًا تتيح هذه الترويسات للإضافة أن تضيف هدفًا، أو تستبدل خطوة خفض واحدة أو كلها،
أو تراقب كل تعليمة لحظة إصدارها، أو تعرّف لهجة تجميع، أو تعيد كتابة ملف كائن
—عبر واجهة C ثنائية خالصة لا تكشف أبدًا عن `MCInst` أو `MCSection` أو
`object::ObjectFile` من LLVM.

## الواجهات

```c
#include "neverc/Plugin/PluginTarget.h"
#include "neverc/Plugin/PluginMC.h"
#include "neverc/Plugin/PluginObject.h"   /* includes both of the above */
```

| الواجهة | الجدول | الخانات | الغرض |
|---|---|--:|---|
| `NEVERC_INTERFACE_TARGET_*` | `NevercTargetAPI` | 2 | `RegisterTarget`، `RegisterCodeGenEdge` |
| `NEVERC_INTERFACE_TARGET_ABI_*` | `NevercTargetABIAPI` | 1 | `RegisterABI` |
| `NEVERC_INTERFACE_CALLING_CONVENTION_*` | `NevercCallingConventionAPI` | 1 | `RegisterCallingConvention` |
| `NEVERC_INTERFACE_MC_*` | `NevercMCAPI` | 53 | قراءة `MCUnit` وتعديله؛ تسجيل المرمِّزات والمفكِّكات والواجهات الخلفية |
| `NEVERC_INTERFACE_MC_EMISSION_*` | `NevercMCEmissionAPI` | 7 | أحداث الإصدار ولقطات التخطيط |
| `NEVERC_INTERFACE_MC_PROVIDER_*` | `NevercMCProviderAPI` | 4 | استبدال `MIR → MC` |
| `NEVERC_INTERFACE_ASSEMBLY_PROVIDER_*` | `NevercAssemblyProviderAPI` | 8 | استبدال محلِّل التجميع أو طابعه |
| `NEVERC_INTERFACE_OBJECT_*` | `NevercObjectAPI` | 34 | قراءة ObjectGraph وتعديله |
| `NEVERC_INTERFACE_OBJECT_FORMAT_*` | `NevercObjectFormatAPI` | 1 | `RegisterFormat` |
| `NEVERC_INTERFACE_OBJECT_PHASE_*` | `NevercObjectPhaseAPI` | 2 | `GetGraph`، `GetImage` |

## مستويا التوافق

هذه هي القاعدة التي تحكم كل ما عداها هنا.

**STABLE**، ويمكن تثبيتها في الشيفرة بأمان: الواصفات المستقلة عن الهدف، ومعرِّفات
المراحل، ومعرِّفات القطع الأثرية، وحاويتا MC وObjectGraph، ومعاملات الإخراج، وكل
عقد رد نداء.

**LOCKSTEP**، وغير آمنة بلا فحص: مخططات الأكواد التشغيلية والسجلات والمعاملات
وعمليات الإصلاح وإعادة التموضع واصطلاحات الاستدعاء الخاصة بالهدف. قيمها العددية لا
تعني شيئًا إلا مقابل مراجعة مخطط واحدة بعينها.

في كل موضع تظهر فيه قيمة LOCKSTEP تظهر بجوارها بصمة مخطط. قارنها قبل قراءة القيمة:

```c
if (!string_equal(Target.SchemaDigest, MY_COMPILED_SCHEMA_DIGEST))
  return fail(NEVERC_STATUS_ABI_MISMATCH);
```

كما ترفض NeverC المخطط غير المتطابق قبل استدعاء المزوِّد، فالفحص إذًا حزام وحمّالة
معًا — لكن الإضافة التي تتخطاه وتقرأ كودًا تشغيليًا خامًا على أي حال ستُسيء تفسير
التعليمات بصمت.

## المراحل

تسع وعشرون، في أربعة مجالات.

### `codegen` — التوجيه (4)

| المرحلة | السياسة |
|---|---|
| `neverc.codegen.ir_to_mir` | OBSERVABLE، INTERCEPTABLE، REPLACEABLE |
| `neverc.codegen.mir_to_mc` | OBSERVABLE، INTERCEPTABLE، REPLACEABLE |
| `neverc.codegen.coarse_lower` | OBSERVABLE، INTERCEPTABLE، REPLACEABLE |
| `neverc.codegen.product_verify` | OBSERVABLE، **SEALED** |

### `mc` — شيفرة الآلة (13)

المراحل `neverc.mc.encode` و`neverc.mc.decode` و`neverc.mc.layout` هي
OBSERVABLE وINTERCEPTABLE وREPLACEABLE.

و`neverc.mc.emission.pre_instruction` هو حدث الإصدار الوحيد الذي يكون
REPLACEABLE أيضًا — وهناك تستبدل التعليمة. أما التسعة الأخرى (`unit_begin`
و`unit_end` و`section_change` و`post_instruction` و`post_encode` و`fixup`
و`relaxation_round` و`pre_layout` و`post_layout`) فللمراقبة فقط.

### `assembly` (4)

`neverc.assembly.parse` و`neverc.assembly.print` قابلتان للاستبدال.
و`neverc.assembly.final_verify` و`neverc.assembly.commit` مختومتان.

### `object` (8)

`neverc.object.probe` و`read` و`write` و`pre_write` و`post_layout` قابلة
للاستبدال؛ و`neverc.object.post_write` قابلة للاعتراض فقط؛ و
`neverc.object.final_verify` و`neverc.object.commit` مختومتان.

## تسجيل هدف

`NevercTargetDescriptor` هو أكبر واصف في هذه الواجهة الثنائية، لأنه يحمل كل ما
تحتاج الواجهتان الأمامية والخلفية إلى معرفته:

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

يقرِّر `TripleMatchers` متى يُختار الهدف: كل مُطابِق يسمّي معمارية ومورِّدًا ونظام
تشغيل وبيئة، إضافة إلى `Priority` يفضّ التعادل في مواجهة الأهداف المدمجة.

أما `Machine` فهو `NevercTargetMachineDescriptor` — تخطيط البيانات، ووحدات
المعالجة الافتراضية والمخصّصة للضبط، وجدول الميزات، وواجهات ABI واصطلاحات
الاستدعاء وصيغ الكائنات المدعومة، وفضاءات العناوين، ونموذجا إعادة التموضع والشيفرة
(بوصفهما قيمة افتراضية وقناع دعم معًا)، ونموذج الاستثناءات (`NONE` و`DWARF`
و`SJLJ` و`SEH` و`WASM`)، ونموذج فك الكدسة، وترتيب البايتات، وعرض
pointer/int/long/long long، ومحاذاة المكدس، وأقصى عرض ذرّي وشعاعي، ونوع
`va_list`، ومستويات التنفيذ (`USER` و`KERNEL` و`HYPERVISOR` و`FIRMWARE`)، ودعم
TLS.

وتحمل دوال الهدف المدمجة رد نداء الخفض الخاص بها، وهو يتلقّى بانيَ IR حيًّا:

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

## ABI واصطلاحات الاستدعاء

تُصنِّف واجهة ABI تواقيع الدوال:

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

أنواع الوسائط هي `DIRECT` و`EXTEND` و`INDIRECT` و`IGNORE` و`EXPAND`
و`INDIRECT_ALIASED` و`COERCE_AND_EXPAND`؛ والرايات هي `BYVAL` و`REALIGN`
و`INREG` و`SRET_AFTER_THIS` و`CAN_BE_FLATTENED` و`SIGN_EXTEND`
و`PADDING_INREG`. والإكراه إما `NONE` أو `INTEGER` أو `FLOAT` أو `POINTER`،
ويقدّم `COERCE_AND_EXPAND` مصفوفة من `NevercABICoercionElement`.

أما اصطلاح الاستدعاء فينزل مستوى أدنى ويخصّص المواقع الفعلية:

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

الحقل `Query->SchemaDigest` قيمة LOCKSTEP — و`RegisterNumber` لا يعني شيئًا إلا
مقابل المخطط الذي يسمّيه. للاطلاع على المثال الكامل انظر
[اصطلاحات استدعاء مخصّصة](custom-callconv/README.ar.md#الخطط-المُجسَّدة) و
[`pluginsdk/examples/CustomCallConvPlugin.c`].

## مسارات توليد الشيفرة

يُختار المسار من `NevercTargetKey` القياسي: معرِّف الهدف، وأجزاء الثلاثية، ووحدة
المعالجة، ووحدة معالجة الضبط، والميزات، وABI، واصطلاح الاستدعاء، وصيغة الكائن،
ونموذج إعادة التموضع، ونموذج الشيفرة، ومستوى التنفيذ، وعرض المؤشر، وترتيب
البايتات، وبصمة المخطط. سجِّل الحوافّ التي تستطيع خدمتها:

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

أنواع النواتج هي `IR` و`MIR` و`MC` و`ASSEMBLY` و`OBJECT_GRAPH`
و`OBJECT_IMAGE` و`CUSTOM`. والمسار الدقيق هو
`IR → MIR → MC → ObjectGraph → ObjectImage`.

وضبط `NEVERC_CODEGEN_EDGE_COARSE` مع تقديم `CoarseLower` يستبدل امتداد
`IR → ObjectImage` كله في خطوة واحدة:

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

ومع ذلك يمرّ المسار الخشن عبر `neverc.codegen.product_verify` وعبر إيداع الإخراج
المعاملاتي. ويُستدعى `VerifyProduct` مصحوبًا بالالتزامات التي يتوقّع المُضيف أنك
أوفيت بها — `VERIFY_FINAL_IR` و`VERIFY_TARGET_KEY` و`VERIFY_PRODUCT_KIND` و
`VERIFY_PRODUCT_ID` و`VERIFY_STRUCTURE` — فلا يستطيع مزوِّد أن يتخطّى بوابةً
خِلسةً بسلوك طريق مختصر.

## بناء MC

يحتوي `MCUnit` على أقسام ورموز وتعابير وشُذرات وتعليمات ومعاملات وعمليات إصلاح.
والقراءة تكرار بنمط first/next:

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

والتعديل معاملاتي، كما في كل مكان آخر:

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

المقابض محدودة بنطاق المهمة ومفحوصة بالأجيال، فالمقبض الآتي من تعديل مهجور
يُرفَض بدل أن يُعاد استخدامه.

رايات الأقسام هي `ALLOCATED` و`EXECUTABLE` و`WRITABLE` و`MERGEABLE` و`DEBUG`.
وارتباطات الرموز هي `LOCAL` و`GLOBAL` و`WEAK`؛ وأنواعها `NONE` و`FUNCTION`
و`OBJECT` و`SECTION` و`TLS`؛ وتعريفاتها `UNDEFINED` و`SECTION` و`ABSOLUTE`
و`COMMON`. وتدعم التعابير العمليات الأحادية `PLUS` و`MINUS` و`NOT`، والثنائية
`ADD` و`SUBTRACT` و`MULTIPLY` و`DIVIDE` و`AND` و`OR` و`XOR` و`SHIFT_LEFT`
و`SHIFT_RIGHT`. ومرِّر `NEVERC_MC_AUTOMATIC_OFFSET` حيث تريد أن يضع المُضيف شيئًا
نيابة عنك.

ينشر `RegisterSchema` مخطط MC للهدف، ويحلّ `GetSchemaToken` /
`GetSchemaTokenInfo` الاسم إلى رمز LOCKSTEP والعكس.

## مراقبة الإصدار

يُبلِّغ تدفّق الإصدار عن عشرة أنواع من الأحداث بالترتيب — نوع لكل مرحلة
`neverc.mc.emission.*`. كما تحجز الواجهة الثنائية
`NEVERC_MC_EMISSION_PRE_OBJECT_WRITE`؛ وكتابة الكائن نفسها هي المرحلة المنفصلة
`neverc.object.pre_write`. اشترك بصفتك مراقبًا
واقرأ الحدث:

```c
NevercMCEmissionEventInfo Event = {0};
Event.Header = /* … */;
Emission->GetEvent(Emission->Context, Frame, Frame->Input, &Event);
/* Event.Kind, Event.Flags */
```

يخبرك `Flags` بأي أجزاء الحدث مُعبَّأة: `HAS_SECTION` و`HAS_INSTRUCTION`
و`HAS_ENCODING` و`HAS_FIXUP` و`HAS_LAYOUT` و`CAN_REPLACE_INSTRUCTION`. تحقَّق من
الراية قبل قراءة الحقل المقابل — فالحدث الذي لا ترميز له بعد لن يصير له ترميز
لمجرد أنك سألت.

وتعطي `GetLayoutSection` و`GetLayoutFragment` و`GetLayoutSymbol` و
`GetLayoutFixup` العناوين والأحجام متى ضُبطت `HAS_LAYOUT`.

وعند `pre_instruction`، وفقط حين تكون `CAN_REPLACE_INSTRUCTION` مضبوطة، يمكنك
الاستبدال:

```c
const NevercMCAPI *MC;
NevercMCUnitHandle Unit;
NevercMCInstHandle Instruction;
Emission->BeginInstructionReplacement(Emission->Context, Frame, Continuation,
                                       &MC, &Unit, &Instruction);
/* mutate Instruction through MC->BeginMutation / … / CommitMutation */
Emission->PublishInstructionReplacement(Emission->Context, Frame, Continuation,
                                         &OutResult->Output);
```

و[`pluginsdk/examples/MCObserverPlugin.c`] هو النسخة للقراءة فقط من هذا.

## المرمِّزات والمفكِّكات والتخطيط

ثلاث عمليات تسجيل توسّع الواجهة الخلفية لشيفرة الآلة، وكلها مفهرسة بالهدف وببصمة
المخطط:

```c
MC->RegisterEncoder(MC->Context, RegistrarContext, &EncoderDescriptor);
MC->RegisterDecoder(MC->Context, RegistrarContext, &DecoderDescriptor);
MC->RegisterAsmBackend(MC->Context, RegistrarContext, &BackendDescriptor);
```

يكتب المرمِّز عبر مصرف بدل أن يعيد مخزنًا مؤقتًا، وهو ما يُبقي الملكية في جانب
المُضيف:

```c
Sink->WriteBytes(Sink->Context, Bytes);
Sink->AddFixup(Sink->Context, &Fixup);
```

ويُبلِّغ المفكِّك بواحدة من `NEVERC_MC_DECODE_SUCCESS` أو `_SOFT_FAIL` أو
`_UNKNOWN` أو `_FAIL`. وتصف أنواع الإصلاح نفسها عبر `NevercMCFixupKindInfo`
برايات `PC_RELATIVE` و`SIGNED` و`RELAXABLE` و`TARGET`.

وتملك الواجهة الخلفية للتجميع عملية الإرخاء. ويُصدر التخطيط بصمة إثبات، و**أي
تعديل بعد التخطيط يُبطل ذلك الإثبات** ويفرض إعادة تخطيط قبل أن يمكن كتابة الكائن
— وهو نفس نمط الفحص بالأجيال الذي يستعمله رسم الربط.

## التجميع

يستهلك مزوِّد التحليل بايتات المصدر وينشر `MCUnit`:

```c
NevercAssemblyParseInputInfo In = {0};
In.Header = /* … */;
Asm->GetParseInput(Asm->Context, Frame, Frame->Input, &In);

NevercAssemblyTokenInfo Token = {0};
Asm->PeekSourceToken(Asm->Context, Frame, In.Source.Cursor, &Token);
Asm->AdvanceSourceToken(Asm->Context, Frame, In.Source.Cursor);

const NevercMCAPI *MC;
NevercMCUnitHandle Unit;
Asm->GetParseMCBuilder(Asm->Context, Frame, &MC, &Unit);
/* … build into Unit … */
Asm->PublishParsedMCUnit(Asm->Context, Frame, &Output);
```

المصادر إما `NEVERC_ASSEMBLY_SOURCE_BUFFER` وإما
`NEVERC_ASSEMBLY_SOURCE_RENDERED_TOKENS`. والتجميع المعالَج مسبقًا (`.S`) يمرّ
أولًا عبر المعالج المسبق الأمامي المعتاد ويصل على هيئة رموز مُصيَّرة؛ أما التجميع
الخالص (`.s`) فيدخل المحلِّل مباشرة على هيئة مخزن مؤقت.

ويسير الطابع في الاتجاه المعاكس — `GetPrintInput`، ثم `WritePrintOutput` داخل
معاملة الإخراج المُقدَّمة، ثم `PublishAssemblyOutput`. والكتابة في أي مكان آخر غير
مدعومة: فتدقيق التحليل/الطباعة وبوابة إيداع المُضيف يعملان قبل أن تصير البايتات
مرئية، ولذا لا تترك الطباعة الفاشلة أي ملف ناقص وراءها.

## رسوم الكائنات

يُطبِّع `NevercObjectAPI` الملف القابل لإعادة التموضع إلى أقسام ورموز وعمليات
إعادة تموضع وCOMDAT. وتغطي المحوّلات المدمجة ELF وCOFF وMach-O؛ ويضيف
`RegisterFormat` صيغة أخرى.

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

ويتبع التعديل نمط الإنشاء/الاستبدال/النقل/المحو لأنواع الكيانات الأربعة جميعًا،
مُجهَّزًا داخل `BeginMutation` … `CommitMutation` / `AbandonMutation`.

رايات الأقسام هي `ALLOCATED` و`EXECUTABLE` و`WRITABLE` و`MERGEABLE`
و`STRINGS` و`TLS` و`DEBUG` و`UNWIND` و`DISCARDABLE` و`RETAIN`. وأهداف إعادة
التموضع هي `SYMBOL` أو `SECTION` أو `ABSOLUTE` أو `FORMAT_EXTENSION`.

ولكل واصف ثلاثية `ExtensionOwner` / `ExtensionVersion` / `Extension`. وبهذا
تحتفظ الصيغة ببيانات لا حقل لها في الرسم المُطبَّع — إذ تسافر تلك البايتات مع
الكيان وتعود عند الكتابة، بدل أن تسقط في رحلة الذهاب والإياب.

### تسجيل صيغة

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

يُبلِّغ `Probe` عن `Confidence` من 0 إلى
`NEVERC_OBJECT_PROBE_MAX_CONFIDENCE` (1000)، وعن
`NevercObjectArtifactKind` الذي تعرَّف عليه (`RELOCATABLE` أو `ARCHIVE` أو
`EXECUTABLE_IMAGE` أو `SHARED_IMAGE` أو `UNIVERSAL_BINARY`)، وعن
`ConsumedMinimum` — أي كم بايتًا لزمه ليتيقّن، بحدّ أقصى
`NEVERC_OBJECT_PROBE_MAX_CONSUMED_MINIMUM` (65536). وتفوز أعلى ثقة.

ويُسلَّم `Reader` رسمًا وتعديلًا مفتوحًا فيملأهما. ويُسلَّم `Writer` الرسم وإثبات
تخطيطه والباني الثنائي المحدود.

### خط أنابيب الكتابة

1. الاستكشاف وقراءة البايتات داخل ObjectGraph؛
2. تشغيل معترضات الرسم `object.pre_write`؛
3. التخطيط ثم تشغيل `object.post_layout` (إعادة التخطيط بعد أي تعديل)؛
4. كتابة صورة مرشَّحة محدودة؛
5. تشغيل المعترضات الثنائية `object.post_write`؛
6. تشغيل `object.final_verify` المختوم و`object.commit` الذرّي.

وتنتقل حالة الصورة `CANDIDATE` → `VERIFIED` → `COMMITTED`، أو `ABORTED` /
`FAILED_PARTIAL`.

ويتلقّى المراقبون جسورًا للقراءة فقط؛ وأي تعديل يُحاوَل من مراقب يُرفَض بـ
`NEVERC_STATUS_POLICY_VIOLATION`. ولا يحصل الكُتّاب ومعترضات ما بعد الكتابة إلا
على الباني المحدود `NevercMutableBinaryAPI` — `Reserve` و`Write` و`WriteAt`
و`Tell` و`ReadAt` و`Insert` و`Append` و`Resize`. والفيضان أو فشل رد النداء أو
فشل التدقيق يُجهِض التجهيز، فلا يترك الفشل أبدًا نصف ملف على القرص.

و[`pluginsdk/examples/ObjectRewritePlugin.c`] مثال كامل على إعادة كتابة معاملاتية.

## القواعد

- قارن بصمة المخطط قبل استهلاك أي قيمة LOCKSTEP لكود تشغيلي أو سجل أو معامل أو
  إصلاح أو إعادة تموضع أو اصطلاح استدعاء.
- احفظ الحالة القابلة للتغيير في حالة process وsession وtask التي يوفّرها
  المُضيف.
- لا تُخزِّن مقابض المهام ولا العروض المُستعارة بعد عودة رد النداء.
- استدعِ استمرارية المُعترِض مرة واحدة على الأكثر، وفي خيط رد النداء.
- كل `BeginMutation` يبلغ إيداعًا واحدًا بالضبط أو هجرًا واحدًا بالضبط.
- أعِد التخطيط بعد تعديل `MCUnit` أو ObjectGraph سبق تخطيطه؛ فإثبات التخطيط
  القديم بات قديمًا وسيرفضه المُضيف.
- تحقَّق من `NevercMCEmissionEventInfo.Flags` قبل قراءة أي حقل حدث، ولا تستبدل
  تعليمة إلا حين تكون `CAN_REPLACE_INSTRUCTION` مضبوطة.
- لا تكتب الإخراج إلا عبر المعاملة أو مصرف البايتات المُقدَّم.
- أعِد `NevercStatus` الأصلي عند الفشل ولا تنشر شيئًا ناقصًا.
- أعلِن أضيق نموذجَي تزامن وإعادة دخول صادقين.
- المراحل `codegen.product_verify` و`assembly.final_verify` و
  `assembly.commit` و`object.final_verify` و`object.commit` مختومة. راقب فقط.

انظر [`PluginTarget.h`] و[`PluginMC.h`] و[`PluginObject.h`] و
[`Schema/PhaseSchema.json`] للإعلانات المِعيارية؛ وأنواع الكيانات والمعاملات
وعمليات الإصلاح والأقسام التي تستخدمها تأتي من [`Schema/MCSchema.json`]
و[`Schema/ObjectSchema.json`]، اللذين يولّدان [`Schema/PluginMCSchema.inc`]
و[`Schema/PluginObjectSchema.inc`]. و[`coverage.json`] يربط كل مرحلة من هذه
المراحل المستقرة باختباراتها الإيجابية والسلبية والاستبدالية والمراقِبة
للقراءة فقط واختبارات البوابات المختومة.

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

</div>
