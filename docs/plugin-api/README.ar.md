<div dir="rtl">

**اللغات**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← فهرس التوثيق](../README.ar.md) · [← مشروع NeverC](../i18n/README.ar.md)

# واجهة NeverC الثنائية للإضافات

إضافة NeverC هي وحدة مشتركة تُصدِّر دالة واحدة بالضبط، وتتفاوض على جداول قدرات
مُرقَّمة الإصدارات عبر معرِّف واجهة بطول 128 بت، وتربط نفسها برسم بياني مُجمَّد من
مراحل المُصرِّف المُسمّاة. الواجهة بأكملها هي C11 خالصة. الإضافة لا تُضمِّن أبدًا ترويسة
من LLVM، ولا تربط المُصرِّف، ولا تُمرِّر نوعًا من C++ عبر الحدود.

```c
NEVERC_EXPORT NevercStatus NEVERC_CALL
neverc_plugin_entry(const NevercBootstrapAPI *Bootstrap,
                    NevercPluginDescriptor *OutPlugin);
```

هذا التوقيع، المُعلَن في [`PluginCore.h`]، هو عقد الربط بأكمله. كل ما عداه — قراءة
الـ IR، وإعادة كتابة رسم الكائنات، واستبدال خط أنابيب التحسين — يُبلَغ عبر جداول
تطلبها من المُضيف بالمعرِّف.

## الأدلة

| الدليل | ما يغطيه |
|---|---|
| [واجهة المُشغِّل](driver.ar.md) | سطر الأوامر، اختيار سلسلة الأدوات، رسم الإجراءات، رسم المهام |
| [واجهة المصادر والإدخال/الإخراج](source.ar.md) | مزوِّدو VFS، مواقع المصدر، المخازن المؤقتة، مصارف الإخراج، التبعيات |
| [واجهة المعالج المسبق](prep.ar.md) | الرموز، الماكرو، البراغما، التضمينات، استعلامات الميزات، 39 نوعًا من الأحداث |
| [واجهة الشجرة النحوية والدلالات](ast-sema.ar.md) | توسيع المُحلِّل، تعديل الشجرة النحوية، البحث عن الأسماء، الأنواع، الثوابت |
| [واجهة IR](ir.ar.md) | قراءة LLVM IR، البناء المعاملاتي، التحليلات، المرورات، المزوِّدون |
| [واجهة MIR](mir.ar.md) | دوال الآلة، السجلات، إطارات المكدس، مرورات وتحليلات MIR |
| [الهدف وMC والتجميع والكائنات](target-mc-object.ar.md) | تسجيل الأهداف، اصطلاحات الاستدعاء، ترميز MC، رسوم الكائنات |
| [واجهة الربط وLTO](link-lto.ar.md) | رسم الربط، حل الرموز، GC/ICF، مزوِّدو الرابط وLTO |
| [واجهة DynCode](dyncode.ar.md) | صور مسطّحة مستقلة عن الموضع، خفض الاستيرادات، ترميز مجموعة المحارف |
| [اصطلاحات استدعاء مخصّصة](custom-callconv/README.ar.md) | إضافات اصطلاحات الاستدعاء المُوجَّهة بالبيانات |
| [أدلة تغطية المراحل](coverage.json) | ربط الاختبارات بكل مرحلة مستقرة |

## نموذج التنفيذ

يقود المُضيف الإضافة عبر ثلاثة نطاقات متداخلة. كل نطاق يُسلِّم الإضافة مؤشر حالة
مُعتِمًا تُخصِّصه الإضافة وتملكه، ولذلك لا تحتاج الإضافة المكتوبة بشكل صحيح إلى أي
حالة عامة قابلة للتغيير.

| النطاق | ردود النداء | المعنى |
|---|---|---|
| Process | `ProcessBegin`، `Register`، `Destroy` | عملية مُصرِّف واحدة. هنا تُستعلَم الواجهات وتُسجَّل القدرات. |
| Session | `SessionBegin`، `SessionEnd` | استدعاء واحد للمُشغِّل. |
| Task | `TaskBegin`، `TaskEnd` | وحدة عمل واحدة، يُعرِّفها `NevercTaskKind`. |

```c
typedef struct NevercPluginDescriptor {
  NevercABITableHeader Header;
  NevercStringView PluginID;
  NevercStringView DisplayName;
  NevercSemanticVersion Version;
  NevercConcurrencyModel Concurrency;
  NevercReentrancyModel Reentrancy;
  NevercStructArrayView RequiredInterfaces;   /* NevercInterfaceRequirement[] */
  NevercStructArrayView OptionalInterfaces;   /* NevercInterfaceRequirement[] */
  NevercStructArrayView Dependencies;         /* NevercPluginDependency[]     */
  NevercProcessBeginFn ProcessBegin;
  NevercRegisterPluginFn Register;
  NevercSessionBeginFn SessionBegin;
  NevercSessionEndFn SessionEnd;
  NevercTaskBeginFn TaskBegin;
  NevercTaskEndFn TaskEnd;
  NevercPluginDestroyFn Destroy;
} NevercPluginDescriptor;
```

عمليًا، `PluginID` و`Register` وحدهما إلزاميان؛ ويجوز لأي خانة رد نداء أن تبقى
`NULL`. أنواع المهام هي `NEVERC_TASK_INVOCATION` و`TRANSLATION_UNIT` و`LTO`
و`LINK` و`CODEGEN` و`OBJECT` و`DYNCODE`.

يستدعي المُضيف `ProcessBegin` أولًا، ثم `Register` مرة واحدة بالضبط. التسجيل هو
الموضع الوحيد الذي يجوز فيه إضافة الخيارات والمراقبين والمعترضين والمزوِّدين؛ وبعده
يُجمَّد رسم المراحل.

تُسترجَع الحالة داخل رد النداء بدلًا من التقاطها مسبقًا:

```c
Core->GetSessionState(Core->Context, Frame->Session, PluginID, &SessionState);
Core->GetTaskState(Core->Context, Frame->Task, PluginID, &TaskState);
```

## المراحل

المرحلة انتقال مُسمّى ومُرقَّم الإصدار من قطعة أثرية مُدخَلة إلى قطعة أثرية مُخرَجة.
تشحن NeverC **130 مرحلة مدمجة**، إضافة إلى 8 عائلات معرِّفات توسعة محجوزة للمراحل
التي تُعرِّفها الإضافات:

| المجال | المراحل | المجال | المراحل |
|---|--:|---|--:|
| `driver` | 6 | `mir` | 10 |
| `source` | 3 | `codegen` | 4 |
| `prep` | 6 | `mc` | 13 |
| `syntax` | 7 | `assembly` | 4 |
| `sema` | 7 | `object` | 8 |
| `ir` | 8 | `link` | 20 |
| | | `dyncode` | 34 |

المراحل الـ130 جميعها من مستوى الاستقرار `stable` في الإصدار الرئيسي 1 من
الواجهة الثنائية. كل مرحلة تُعلن سياسة، ولا يجوز للإضافة أن ترتبط إلا بالطرق التي
تسمح بها تلك السياسة:

| راية السياسة | المراحل | ما يجوز للإضافة |
|---|--:|---|
| `NEVERC_PHASE_OBSERVABLE` | 130 | تسجيل مراقب لتلقّي إشعار للقراءة فقط. |
| `NEVERC_PHASE_INTERCEPTABLE` | 105 | تغليف المرحلة وتقرير ما إذا كانت بقية السلسلة ستُستدعى. |
| `NEVERC_PHASE_REPLACEABLE` | 86 | تسجيل مزوِّد يُنتج المُخرَج بنفسه. |
| `NEVERC_PHASE_SKIPPABLE_WITH_PROOF` | 13 | تخطّي الانتقال مع تقديم مقبض إثبات. |
| `NEVERC_PHASE_SEALED_HOST_GATE` | 14 | لا شيء. المُدقِّقون والإيداعات ملك للمُضيف. |

البوابات المختومة الأربع عشرة هي `ir.final_verify` و`mir.final_verify`
و`codegen.product_verify` و`assembly.final_verify` و`assembly.commit`
و`object.final_verify` و`object.commit` و`link.image_verify`
و`link.side_outputs_verify` و`link.commit` و`dyncode.ir.final_verify`
و`dyncode.mir.final_verify` و`dyncode.verify` و`dyncode.commit`. يمكن مراقبتها،
لكن لا يمكن اعتراضها أو استبدالها أو تخطّيها أبدًا.

يُبلَّغ المراقبون عند النقاط التي تُعلنها المرحلة: `NEVERC_OBSERVER_BEFORE`
و`NEVERC_OBSERVER_AFTER` و`NEVERC_OBSERVER_AFTER_COMMIT`. ويتلقّى المُعترِض
`NevercPhaseContinuation`، وعليه استدعاء `InvokeNext` **مرة واحدة على الأكثر**،
في خيط رد النداء، ثم الإبلاغ عن `NEVERC_PHASE_CONTINUE` أو
`NEVERC_PHASE_REPLACE` أو `NEVERC_PHASE_SKIP` في `NevercPhaseResult.Action`.

يتلقّى كل رد نداء مرحلة الإطار نفسه:

```c
typedef struct NevercPhaseFrame {
  NevercABITableHeader Header;
  NevercSessionHandle Session;
  NevercTaskHandle Task;
  NevercInterfaceID Phase;
  NevercPhaseRoute Route;        /* triple, CPU, features, object format */
  NevercArtifactHandle Input;
  NevercArtifactHandle CurrentOutput;
  NevercHandle Cancellation;
} NevercPhaseFrame;
```

الملف [`Schema/PhaseSchema.json`] هو المصدر المِعياري لمعرِّفات المراحل والسياسات
ومستويات الاستقرار وبوابات التدقيق. والملف المُولَّد
[`Schema/PluginPhaseSchema.inc`] يكشف كلًّا منها كثابت وقت التصريف — للمرحلة
`neverc.ir.pass.pipeline_start`:

```c
NEVERC_PHASE_IR_PASS_PIPELINE_START_NAME       /* "neverc.ir.pass.pipeline_start" */
NEVERC_PHASE_IR_PASS_PIPELINE_START_HIGH       /* UINT64_C(0x4e43504849520001)     */
NEVERC_PHASE_IR_PASS_PIPELINE_START_LOW        /* UINT64_C(0x0000000000000004)     */
NEVERC_PHASE_IR_PASS_PIPELINE_START_POLICY     /* OBSERVABLE | INTERCEPTABLE       */
NEVERC_PHASE_IR_PASS_PIPELINE_START_STABILITY
NEVERC_PHASE_IR_PASS_PIPELINE_START_INPUT_HIGH /* and _INPUT_LOW, _OUTPUT_*        */
```

يتيح الثابت `NEVERC_BUILTIN_PHASE_COUNT` والثوابت الخاصة بكل مجال
`NEVERC_BUILTIN_<DOMAIN>_PHASE_COUNT` للإضافة أن تؤكّد الرسم الذي صُرِّفت مقابله.

## إضافة صغرى كاملة

هذا هو [`pluginsdk/templates/minimal/Plugin.c`] حرفيًا. يُحمَّل، ويتفاوض على الواجهة
الثنائية، ولا يُسجِّل شيئًا، ويُفرَّغ بنظافة — انسخ المجلد وابنِ عليه من هنا.

```c
#include "neverc/Plugin/NevercPluginAPI.h"

#define MINIMAL_PLUGIN_ID "com.example.minimal"
#define STRING_VIEW_LITERAL(Text)                                              \
  { (Text), (uint64_t)(sizeof(Text) - 1) }

static NevercStatus status_code(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

static void copy_bytes(void *Destination, const void *Source, uint64_t Count) {
  uint64_t Index;
  unsigned char *Out = (unsigned char *)Destination;
  const unsigned char *In = (const unsigned char *)Source;
  for (Index = 0; Index != Count; ++Index)
    Out[Index] = In[Index];
}

static NevercStatus NEVERC_CALL
process_begin(const NevercCoreAPI *Core, void **OutProcessState) {
  if (Core == NULL || OutProcessState == NULL)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutProcessState = NULL;
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL
register_plugin(const NevercCoreAPI *Core, const NevercRegistrarAPI *Registrar,
                void *RegistrarContext, void *ProcessState) {
  (void)Core;
  (void)RegistrarContext;
  (void)ProcessState;
  if (Registrar == NULL)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  /* Register options, observers, interceptors, or providers here. */
  return neverc_status_ok();
}

NEVERC_EXPORT NevercStatus NEVERC_CALL
neverc_plugin_entry(const NevercBootstrapAPI *Bootstrap,
                    NevercPluginDescriptor *OutPlugin) {
  NevercPluginDescriptor Descriptor = {0};
  uint32_t Capacity;
  uint64_t BytesToWrite;
  if (Bootstrap == NULL || OutPlugin == NULL ||
      OutPlugin->Header.StructSize < sizeof(uint32_t))
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  Capacity = OutPlugin->Header.StructSize;
  Descriptor.Header = (NevercABITableHeader){
      sizeof(Descriptor), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Descriptor.PluginID = (NevercStringView)STRING_VIEW_LITERAL(MINIMAL_PLUGIN_ID);
  Descriptor.DisplayName =
      (NevercStringView)STRING_VIEW_LITERAL("Minimal Plugin");
  Descriptor.Version = (NevercSemanticVersion){1, 0, 0, 0};
  Descriptor.Concurrency = NEVERC_CONCURRENCY_SESSION_SERIAL;
  Descriptor.Reentrancy = NEVERC_REENTRANCY_ALLOWED;
  Descriptor.ProcessBegin = process_begin;
  Descriptor.Register = register_plugin;
  BytesToWrite = Capacity < sizeof(Descriptor) ? Capacity : sizeof(Descriptor);
  copy_bytes(OutPlugin, &Descriptor, BytesToWrite);
  return neverc_status_ok();
}
```

الوسيط `OutPlugin` مخزن مؤقت يملكه المُستدعي. عند الدخول تكون قيمة
`Header.StructSize` فيه هي السعة القابلة للكتابة؛ فتكتب الإضافة هذا العدد من
البايتات على الأكثر وتُبلِغ عن الحجم الذي أنتجته فعلًا. وكتابة `Header` الواصف
نفسه أولًا ثم اقتطاع النسخة يُحقِّق شطري هذه القاعدة معًا.

## التفاوض على الواجهات

تُجلَب جداول القدرات بمعرِّف واجهة بطول 128 بت، لا بالرمز. اطلب الإصدار الرئيسي
الذي صرَّفت مقابله، وأدنى إصدار فرعي يمكنك العمل به:

```c
const void *Table = NULL;
uint16_t Minor = 0;
uint64_t TableSize = 0;
NevercStatus Status = Bootstrap->QueryInterface(
    Bootstrap->Context,
    (NevercInterfaceID){NEVERC_INTERFACE_IR_PASS_HIGH,
                        NEVERC_INTERFACE_IR_PASS_LOW},
    NEVERC_IR_PASS_API_MAJOR, NEVERC_IR_PASS_API_MINOR, &Table, &Minor,
    &TableSize);
if (Status.Code != NEVERC_STATUS_OK)
  return Status;
if (!Table || TableSize < offsetof(NevercIRPassAPI, RegisterPass) +
                              sizeof(((NevercIRPassAPI *)0)->RegisterPass))
  return fail(NEVERC_STATUS_ABI_MISMATCH);
```

مقارنة `TableSize` بإزاحة آخر دالة تستدعيها هي القاعدة التي تجعل هذه الواجهة
الثنائية قابلة للتوسعة: يُلحق المُضيف الأحدث حقولًا في النهاية، وتظل الإضافة الأقدم
تعمل لأنها لا تقرأ أبدًا خارج البادئة التي تحقَّقت منها. والماكرو
`NEVERC_ABI_FIELD_AVAILABLE(header, type, field)` يُطبِّق الاختبار نفسه على بنية
تلقّيتها. كما أن التوقيع ذاته لـ`QueryInterface` موجود في `NevercCoreAPI`، فيمكنك
التفاوض لاحقًا بدلًا من نقطة الدخول.

الواجهات العامة وجداولها وماكروهات معرِّفاتها:

| زوج ماكرو الواجهة | الجدول | الترويسة |
|---|---|---|
| `NEVERC_INTERFACE_CORE_{HIGH,LOW}` | `NevercCoreAPI` | [`PluginCore.h`] |
| `NEVERC_INTERFACE_DRIVER_*` | `NevercDriverAPI` | [`PluginDriver.h`] |
| `NEVERC_INTERFACE_IO_*`، `..._SOURCE_LOCATION_*` | `NevercIOAPI`، `NevercSourceLocationAPI` | [`PluginSource.h`] |
| `NEVERC_INTERFACE_PREP_*` | `NevercPrepAPI` | [`PluginPrep.h`] |
| `NEVERC_INTERFACE_AST_*`، `..._PARSER_*` | `NevercASTAPI`، `NevercParserAPI` | [`PluginAST.h`] |
| `NEVERC_INTERFACE_SEMA_*` | `NevercSemaAPI` | [`PluginSema.h`] |
| `NEVERC_INTERFACE_IR_CORE_*`، `..._IR_BUILDER_*`، `..._IR_ANALYSIS_*`، `..._IR_PASS_*`، `..._IR_GEN_*`، `..._IR_OPTIMIZATION_*` | ستة جداول IR | [`PluginIR.h`] |
| `NEVERC_INTERFACE_TARGET_*`، `..._TARGET_ABI_*`، `..._CALLING_CONVENTION_*` | `NevercTargetAPI`، `NevercTargetABIAPI`، `NevercCallingConventionAPI` | [`PluginTarget.h`] |
| `NEVERC_INTERFACE_MIR_*`، `..._MIR_ANALYSIS_*`، `..._MIR_PASS_*`، `..._MIR_PROVIDER_*` | أربعة جداول MIR | [`PluginMIR.h`] |
| `NEVERC_INTERFACE_MC_*`، `..._MC_EMISSION_*`، `..._MC_PROVIDER_*`، `..._ASSEMBLY_PROVIDER_*` | أربعة جداول MC | [`PluginMC.h`] |
| `NEVERC_INTERFACE_OBJECT_*`، `..._OBJECT_FORMAT_*`، `..._OBJECT_PHASE_*` | ثلاثة جداول كائنات | [`PluginObject.h`] |
| `NEVERC_INTERFACE_LINK_*`، `..._LINK_REGISTRAR_*`، `..._LINK_PHASE_*` | ثلاثة جداول ربط | [`PluginLink.h`] |
| `NEVERC_INTERFACE_LTO_*`، `..._LTO_REGISTRAR_*` | `NevercLTOAPI`، `NevercLTORegistrarAPI` | [`PluginLTO.h`] |
| `NEVERC_INTERFACE_DYNCODE_*`، `..._DYNCODE_REGISTRAR_*`، `..._DYNCODE_PHASE_*` | ثلاثة جداول dyncode | [`PluginDynCode.h`] |

كما تُعرِّف كل ترويسة قيمتي `NEVERC_<DOMAIN>_API_MAJOR` و`_MINOR` المقابلتين اللتين
ينبغي تمريرهما إلى `QueryInterface`.

الواجهة إما `NEVERC_INTERFACE_STABLE` (لا يجوز للمُضيف الأحدث إلا الإلحاق) وإما
`NEVERC_INTERFACE_LOCKSTEP` (مخططات خاصة بالهدف يجب أن تتطابق تمامًا). قارن بصمة
المخطط قبل استهلاك قيم LOCKSTEP.

## التسجيل

يتلقّى `Register` جدول `NevercRegistrarAPI` وسياق `RegistrarContext` مُعتِمًا:

```c
typedef struct NevercRegistrarAPI {
  NevercABITableHeader Header;
  NevercRegisterInterfaceFn RegisterInterface;
  NevercRegisterPhaseFn RegisterPhase;
  NevercRegisterObserverFn RegisterObserver;
  NevercRegisterInterceptorFn RegisterInterceptor;
  NevercRegisterProviderFn RegisterProvider;
  NevercRegisterOptionFn RegisterOption;
} NevercRegistrarAPI;
```

يأخذ كلٌّ من هذه الاستدعاءات `RegistrarContext` وسيطًا أولًا، ومُوصِّفًا مُصفَّرًا
وسيطًا ثانيًا. والاستدعاء الذي تختاره هو ما يُحدِّد كيف يتعامل المُضيف معك عند
المرحلة:

| الاستدعاء | المُوصِّف | رد النداء | ما يجب أن تُعلنه المرحلة |
|---|---|---|---|
| `RegisterObserver` | `NevercObserverDescriptor` | `NevercPhaseObserverFn` | `OBSERVABLE` |
| `RegisterInterceptor` | `NevercInterceptorDescriptor` | `NevercPhaseInterceptorFn` | `INTERCEPTABLE` |
| `RegisterProvider` | `NevercProviderDescriptor` | `NevercPhaseProviderFn` | `REPLACEABLE` |
| `RegisterPhase` | `NevercPhaseDescriptor` | — | معرِّف تُعرِّفه الإضافة |
| `RegisterOption` | `NevercOptionDescriptor` | `Validator` اختياري | — |
| `RegisterInterface` | وسائط مباشرة | — | — |

المُوصِّف الذي يفشل في التحقّق البنيوي يُرفَض فورًا بالحالة
`NEVERC_STATUS_INVALID_DESCRIPTOR`. أمّا فحص السياسة فيقع عندما يُطبِّق المُضيف
التسجيل: فالمرحلة المجهولة، أو التي لا تُعلن السياسة التي يتطلّبها استدعاؤك،
تُرفَض عندئذٍ. والبوابة المختومة لا تقبل سوى المراقبين.

ومُسجِّلات المجالات — `NevercIRPassAPI.RegisterPass` و
`NevercTargetAPI.RegisterTarget` و`NevercObjectFormatAPI.RegisterFormat` وغيرها
— تأخذ جميعها السياق `RegistrarContext` نفسه وسيطًا ثانيًا، وبه يَنسِب المُضيف
التسجيل إلى إضافتك.

### المراقبون

```c
typedef struct NevercObserverDescriptor {
  NevercABITableHeader Header;
  NevercInterfaceID Phase;
  NevercObserverPoint Points;
  uint32_t Reserved;
  NevercPhaseObserverFn Callback;
  void *UserData;
  NevercDestroyUserDataFn DestroyUserData;
} NevercObserverDescriptor;

typedef NevercStatus(NEVERC_CALL *NevercPhaseObserverFn)(
    const NevercPhaseFrame *Frame, NevercObserverPoint Point, void *UserData);
```

`Points` قناع بتّي مكوّن من `NEVERC_OBSERVER_BEFORE` (1) و`NEVERC_OBSERVER_AFTER`
(2) و`NEVERC_OBSERVER_AFTER_COMMIT` (4)، ويجب ألّا يكون صفرًا؛ ويُخبر الوسيط `Point`
ردَّ النداء أيُّها وقع. من [`pluginsdk/examples/DriverTracePlugin.c`]:

```c
NevercObserverDescriptor Observer = {0};
Observer.Header = (NevercABITableHeader){
    sizeof(Observer), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
Observer.Phase = (NevercInterfaceID){NEVERC_PHASE_DRIVER_RAW_ARGUMENTS_HIGH,
                                     NEVERC_PHASE_DRIVER_RAW_ARGUMENTS_LOW};
Observer.Points = NEVERC_OBSERVER_BEFORE | NEVERC_OBSERVER_AFTER;
Observer.Callback = observe_arguments;
Observer.UserData = Process;
Status = Registrar->RegisterObserver(RegistrarContext, &Observer);
```

يُعاد إليك `UserData` كما هو دون مساس. وضبط `DestroyUserData` — وهو موجود في كل
مُوصِّف في هذا القسم — يجعل المُضيف يُحرِّر تلك الذاكرة عند زوال التسجيل، فلا يعود
يلزم تتبّع تخصيصٍ لكل تسجيل داخل `Destroy`.

### المُعترِضون

```c
typedef struct NevercInterceptorDescriptor {
  NevercABITableHeader Header;
  NevercInterfaceID Phase;
  NevercPhaseInterceptorFn Callback;
  void *UserData;
  NevercDestroyUserDataFn DestroyUserData;
} NevercInterceptorDescriptor;

typedef NevercStatus(NEVERC_CALL *NevercPhaseInterceptorFn)(
    const NevercPhaseFrame *Frame, NevercPhaseContinuation *Continuation,
    NevercPhaseResult *OutResult, void *UserData);
```

المتابعة هي بقية السلسلة كلّها، والنتيجة هي الوسيلة التي تُبلِّغ بها عمّا فعلته بها:

```c
typedef struct NevercPhaseContinuation {
  NevercABITableHeader Header;
  NevercInvokeNextFn InvokeNext;
  void *Context;
  uint64_t Generation;
} NevercPhaseContinuation;

typedef struct NevercPhaseResult {
  NevercABITableHeader Header;
  NevercPhaseAction Action;
  uint32_t Reserved;
  NevercArtifactHandle Output;
  NevercProofHandle Proof;
} NevercPhaseResult;
```

الإجراءات الثلاثة ليست متبادلة. يُقابل المُضيف النتيجة بما فعلته فعليًّا، ويُفشل
السلسلة بالحالة `NEVERC_STATUS_POLICY_VIOLATION` عند أي تعارض:

| `Action` | `InvokeNext` | `Output` | `Proof` | يتطلّب أيضًا |
|---|---|---|---|---|
| `NEVERC_PHASE_CONTINUE` | استُدعي مرة واحدة | فارغ | فارغ | — |
| `NEVERC_PHASE_REPLACE` | لم يُستدعَ | مضبوط | فارغ | `REPLACEABLE` |
| `NEVERC_PHASE_SKIP` | لم يُستدعَ | مضبوط | مضبوط | `SKIPPABLE_WITH_PROOF` |

لا يجوز استدعاء `InvokeNext` أكثر من مرة واحدة، ولا إلّا على خيط رد النداء؛
فالاستدعاء الثاني انتهاكٌ للسياسة، والاستدعاء من خيط آخر يُبلِّغ
`NEVERC_STATUS_WRONG_SCOPE`. كما أنّ المُعترِض الذي يُرجِع `CONTINUE` دون أن
يستدعيه ينتهك السياسة أيضًا، لأنّ المرحلة عندئذٍ لن تُنتِج شيئًا في صمت.

```c
NevercInterceptorDescriptor Interceptor = {0};
Interceptor.Header = (NevercABITableHeader){
    sizeof(Interceptor), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
Interceptor.Phase = (NevercInterfaceID){NEVERC_PHASE_DRIVER_EXECUTE_JOB_HIGH,
                                        NEVERC_PHASE_DRIVER_EXECUTE_JOB_LOW};
Interceptor.Callback = intercept_job;
Interceptor.UserData = Process;
Status = Registrar->RegisterInterceptor(RegistrarContext, &Interceptor);
```

### المزوِّدون

يستبدل المزوِّد المرحلة استبدالًا كاملًا، ولذلك يُعلن أيضًا عقد الحتمية الذي تعتمد
عليه ذاكرة البناء المؤقتة:

```c
typedef struct NevercProviderDescriptor {
  NevercABITableHeader Header;
  NevercInterfaceID Phase;
  NevercStringView ProviderID;
  NevercPhaseRoute Route;
  NevercBool Deterministic;
  NevercBool Cacheable;
  NevercBool FallbackSafe;
  uint32_t Reserved;
  NevercPhaseProviderFn Callback;
  void *UserData;
  NevercDestroyUserDataFn DestroyUserData;
} NevercProviderDescriptor;

typedef NevercStatus(NEVERC_CALL *NevercPhaseProviderFn)(
    const NevercPhaseFrame *Frame, NevercPhaseResult *OutResult,
    void *UserData);
```

```c
Provider.ProviderID    = SV("com.example.my-lowering");
Provider.Route         = /* triple / CPU / features / object format */;
Provider.Deterministic = NEVERC_TRUE;
Provider.Cacheable     = NEVERC_TRUE;
Provider.FallbackSafe  = NEVERC_FALSE;  /* built-in cannot silently take over */
```

يجب أن يكون `ProviderID` اسمًا قانونيًّا: 255 بايتًا على الأكثر من حروف صغيرة
وأرقام و`.` و`_` و`-`، لا يبدأ بنقطة ولا ينتهي بها، ولا يحتوي `..` أبدًا. ويكفي
حرفٌ كبير واحد ليُرفَض التسجيل. ويجب تهيئة `Route.Header` مثل أي ترويسة جدول أخرى.

لا توجد متابعة هنا: فردّ النداء هو المرحلة نفسها. وعليه أن يُبلِّغ
`NEVERC_PHASE_REPLACE` مع `Output` و`Proof` فارغ — وأي شيء غير ذلك انتهاكٌ للسياسة.

`FallbackSafe` هي الراية الوحيدة بين هذه الرايات التي لها أثر في وقت التشغيل يتجاوز
مجرّد التسجيل. فإذا كانت `NEVERC_TRUE` وفشل المزوِّد بحالة موسومة بـ
`NEVERC_STATUS_FLAG_RECOVERABLE`، جاز للمُضيف أن يطرح الآثار الجزئية ويُشغِّل
التنفيذ المدمج بدلًا منه. اتركها `NEVERC_FALSE` متى تعذّر التراجع عن محاولة ناقصة.

### مراحل تُعرِّفها الإضافة

يُضيف `RegisterPhase` انتقالًا لا يعرفه المُضيف، وهذا بالضبط ما حُجزت من أجله
عائلات معرِّفات التوسعة الثماني:

```c
typedef struct NevercPhaseDescriptor {
  NevercABITableHeader Header;
  NevercInterfaceID Phase;
  NevercStringView CanonicalName;
  NevercInterfaceID InputArtifact;
  NevercInterfaceID OutputArtifact;
  NevercPhasePolicy Policy;
  NevercObserverPoint ObserverPoints;
  uint32_t Reserved;
} NevercPhaseDescriptor;
```

يجب ألّا يكون أيٌّ من `Phase` و`InputArtifact` و`OutputArtifact` صفرًا، ويجب أن
تكون `Policy` غير صفرية وألّا تحوي سوى رايات معروفة. ويُرفَض إعلان `ObserverPoints`
دون `NEVERC_PHASE_OBSERVABLE`، كما يُرفَض جمع `NEVERC_PHASE_SEALED_HOST_GATE` مع أيٍّ
من `INTERCEPTABLE` أو `REPLACEABLE` أو `SKIPPABLE_WITH_PROOF` — وهي عين الثوابت التي
يُفحَص بها الرسم المدمج. خُذ المعرِّف من عائلة مجالك حتى لا يصطدم بمرحلة مدمجة
مستقبلية:

```c
#include "neverc/Plugin/Schema/PluginPhaseSchema.inc"

/* NEVERC_EXTENSION_FAMILY_COUNT is 8; family 1 is "neverc.ir.extension". */
NevercInterfaceID MyPhase = {NEVERC_EXTENSION_FAMILY_1_ID_HIGH,
                             NEVERC_EXTENSION_FAMILY_1_ID_LOW_MIN};
```

تنشر كل عائلة `_NAMESPACE` و`_ID_HIGH` و`_ID_LOW_MIN` و`_ID_LOW_MAX`، والنصف الأدنى
متروك لك توزّعه داخل ذلك المجال.

### نشر واجهة للإضافات الأخرى

`RegisterInterface` هو الاستدعاء الوحيد الذي لا يأخذ مُوصِّفًا. فهو يُسلِّم المُضيفَ
جدولًا خاصًّا بك كي تتمكّن إضافة أخرى من الوصول إليه عبر `QueryInterface` نفسه
المستخدَم مع الواجهات المدمجة:

```c
Registrar->RegisterInterface(RegistrarContext, MyInterfaceID,
                             NEVERC_INTERFACE_STABLE, &MyTable,
                             /* Compatibility = */ NULL);
```

مرِّر `NEVERC_INTERFACE_LOCKSTEP` بدلًا من ذلك متى حمل الجدول قيم مخطّط خاصة بالهدف
لا تصمد أمام اختلاف الإصدارات. وعلى واجهة lockstep أن تُوفِّر
`NevercCompatibilityKey` الذي يُثبِّت المستهلك على بناءٍ واحد للمُنتِج:

```c
typedef struct NevercCompatibilityKey {
  NevercABITableHeader Header;
  NevercStringView ProducerBuildID;   /* compare against Bootstrap->HostBuildID */
  NevercStringView TargetABIKey;
  uint32_t LLVMMajor;                 /* compare against Bootstrap->LLVMMajor   */
  uint32_t Reserved;
} NevercCompatibilityKey;
```

يجب ملء الحقول الثلاثة جميعها في تسجيل lockstep؛ فمعرِّف بناء فارغ، أو مفتاح ABI
فارغ، أو إصدار LLVM رئيسي يساوي صفرًا، يُرفَض بوصفه مُوصِّفًا غير صالح.

## البناء

ضمِّن الترويسة الجامعة، أو المجالات التي تستخدمها فقط:

```c
#include "neverc/Plugin/NevercPluginAPI.h"   /* everything */
#include "neverc/Plugin/PluginIR.h"          /* or one domain */
```

بناء وحدة مشتركة بـNeverC نفسه:

```sh
neverc --target=arm64-apple-macosx -shared \
  -I/path/to/pluginsdk/include \
  -o MyPlugin.dylib MyPlugin.c
```

أو مقابل SDK مُثبَّت باستخدام CMake:

```cmake
find_package(NevercPluginSDK REQUIRED)
add_library(my_plugin MODULE my_plugin.c)
target_link_libraries(my_plugin PRIVATE NevercPluginSDK::headers)
```

أو باستخدام pkg-config:

```sh
cc -shared $(pkg-config --cflags neverc-plugin) -o my_plugin.so my_plugin.c
```

استخدم `.so` أو `.dylib` أو `.dll` بحسب المُضيف. لا تربط حزمة التطوير أي LLVM ولا
أي بيئة تشغيل لـNeverC — فـ`NevercPluginSDK::headers` ترويسات فقط.

## التحميل والضبط

```sh
neverc -fplugin=./MyPlugin.dylib -c input.c -o input.o
```

| الخيار | الصيغة | الغرض |
|---|---|---|
| `-fplugin=<path>` | قابل للتكرار | تحميل وحدة إضافة مشتركة لسلسلة الأدوات كاملة. |
| `-fplugin-arg=<plugin-id>:<key>=<value>` | قابل للتكرار | تمرير قيمة ذات فضاء أسماء إلى خيار إضافة مُسجَّل. |
| `-fplugin-provider=<phase>:<plugin-id>` | قابل للتكرار | اختيار الإضافة التي تُزوِّد مرحلة قابلة للاستبدال. |
| `-fplugin-pass=<dsopath>` | قابل للتكرار | تحميل إضافة مرور خارج الشجرة بواجهة C الثنائية. |
| `-fplugin-pass-arg=<key>=<value>` | قابل للتكرار | تمرير وسيط إلى إضافات المرور ذات واجهة C الثنائية. |

لا يجوز حذف المُحدِّد `<plugin-id>:` إلا حين تكون إضافة واحدة بالضبط نشطة.
والخيارات التي تُسجِّلها الإضافة عبر `RegisterOption` تُقبل أيضًا مباشرةً بهجائها
المُعلَن، في صيغة راية أو ملتصقة أو منفصلة أو متعددة الوسائط. أما وسائط الإضافات
واختيارات المزوِّدين بلا `-fplugin=` مقابل فهي خطأ صريح لا عملية تُتجاهل بصمت.

ويمكن إعادة قراءة أي خيار مُسجَّل في أي وقت عبر الجدول الأساسي:

```c
uint64_t Count = 0;
Core->GetPluginOptionValueCount(Core->Context, Session, PluginID,
                                SV("--driver-trace"), &Count);
NevercStringView Value;
Core->GetPluginOptionValue(Core->Context, Session, PluginID,
                           SV("--driver-trace"), 0, &Value);
```

## قواعد الواجهة الثنائية

- استعلم عن جداول القدرات عبر `QueryInterface`؛ واشترط تطابق الإصدار الرئيسي،
  وتحقَّق من `StructSize` قبل لمس أي حقل.
- هيّئ حقل `Header` والمساحة المحجوزة في كل بنية عامة. صفِّر البنية، ثم اضبط
  `StructSize` و`Major` و`Minor` و`Flags`.
- عامِل المقابض والعروض المُستعارة كقيم مُعتِمة محدودة النطاق. لا تحتفظ أبدًا بمقبض
  نطاقه المهمة بعد رد ندائه، ولا تستخدمه في جلسة أو مهمة أخرى، ولا تصطنع قيمة
  مقبض أبدًا.
- أعِد `NevercStatus` من كل رد نداء. ولا تدع استثناء C++ أو مؤشرًا يملكه المُضيف
  يعبر حدود C.
- أعلِن أضيق `NevercConcurrencyModel` صادق (`SESSION_SERIAL` أو `THREAD_SAFE` أو
  `PROCESS_SERIAL`) و`NevercReentrancyModel` (`NONE` أو `ALLOWED`).
- نفِّذ تغييرات IR وMIR والشجرة النحوية والرسوم والقطع الأثرية عبر واجهات المُضيف
  المعاملاتية: ابدأ تعديلًا، وجهِّز التغييرات، ثم أودِع أو أجهِض. الإيداع يُدقِّق وينشر
  بشكل ذرّي؛ والإيداع الفاشل يترك الحالة السابقة سليمة.
- خصِّص الذاكرة عبر `NevercCoreAPI.Allocate` / `Reallocate` / `Deallocate` حين
  يجب أن يحتسبها المُضيف.
- احفظ الحالة القابلة للتغيير في حالة process/session/task التي يوفّرها المُضيف.
  والحالة العامة القابلة للتغيير يفحصها [`utils/plugin-api/check-global-state.py`].

جميع البنى العامة مُرتَّبة تحت `NEVERC_ABI_PACK_BEGIN` (حزم بـ8 بايتات) وبأنواع
ثابتة العرض فقط. وتُلحق الدوال الجديدة بنهاية جداول قدرات مُرقَّمة الإصدارات على نحو
مستقل؛ ولا تتغيّر البادئة المستقرة لأي جدول ضمن الإصدار الرئيسي الأول للواجهة
الثنائية (`NEVERC_PLUGIN_ABI_MAJOR` = 1).

## الحالات والتشخيصات

يحمل `NevercStatus` حقل `Code` وحقل `Flags` وكلمة `Detail`. ومجموعة الرموز
الكاملة:

| الرمز | المعنى |
|---|---|
| `NEVERC_STATUS_OK` | نجاح. |
| `NEVERC_STATUS_INVALID_ARGUMENT` | مؤشر أو قيمة مطلوبة مفقودة أو مشوَّهة. |
| `NEVERC_STATUS_ABI_MISMATCH` | الجدول المُتفاوَض عليه أصغر من اللازم أو الإصدار الرئيسي مختلف. |
| `NEVERC_STATUS_MISSING_INTERFACE` | المُضيف لا ينشر الواجهة المطلوبة. |
| `NEVERC_STATUS_VERSION_MISMATCH` | تعذّر تلبية الإصدار الرئيسي/الفرعي المطلوب. |
| `NEVERC_STATUS_INVALID_DESCRIPTOR` | واصف أخفق في التحقق البنيوي. |
| `NEVERC_STATUS_DUPLICATE_ID` | المعرِّف مُسجَّل من قبل. |
| `NEVERC_STATUS_DEPENDENCY_MISSING` | تبعية مُعلَنة غائبة. |
| `NEVERC_STATUS_DEPENDENCY_CYCLE` | تعذّر تلبية ترتيب التسجيل. |
| `NEVERC_STATUS_BUSY` | مورد محجوز في مكان آخر. |
| `NEVERC_STATUS_CANCELLED` | طُلب إلغاء تعاوني. |
| `NEVERC_STATUS_RESOURCE_EXHAUSTED` | بلغت ميزانية أو حد. |
| `NEVERC_STATUS_STALE_HANDLE` | مقبض عاش أطول من الكائن الذي يُسمّيه. |
| `NEVERC_STATUS_WRONG_SESSION` | استُخدم مقبض في جلسة أخرى. |
| `NEVERC_STATUS_WRONG_SCOPE` | استُخدم مقبض خارج نطاقه. |
| `NEVERC_STATUS_WRONG_TYPE` | مقبض يُسمّي نوعًا آخر من الكيانات. |
| `NEVERC_STATUS_INVALID_STATE` | العملية غير مشروعة في الحالة الراهنة. |
| `NEVERC_STATUS_POLICY_VIOLATION` | سياسة المرحلة تمنع العملية. |
| `NEVERC_STATUS_VERIFICATION_FAILED` | مُدقِّق مختوم لدى المُضيف رفض المنتج. |
| `NEVERC_STATUS_CAPABILITY_UNAVAILABLE` | لا يستطيع المُضيف تقديم هذه القدرة هنا. |
| `NEVERC_STATUS_PLUGIN_FAILURE` | أبلغت الإضافة عن إخفاق عام. |
| `NEVERC_STATUS_PLUGIN_EXCEPTION` | أفلت استثناء من رد نداء الإضافة. |
| `NEVERC_STATUS_OUTPUT_PARTIAL` | كُتب المُخرَج جزئيًا فقط. |
| `NEVERC_STATUS_REENTRANCY_DENIED` | رُفض استدعاء معاود الدخول. |
| `NEVERC_STATUS_NOT_FOUND` | الكيان المُسمّى غير موجود. |

وتصف بتات الرايات ما حدث للمُخرَج، وهو ما يحتاجه نظام البناء ليقرر ما إذا كانت
إعادة المحاولة آمنة: `NEVERC_STATUS_FLAG_RECOVERABLE` و
`_OUTPUT_ALREADY_COMMITTED` و`_OUTPUT_MAY_BE_PARTIAL` و
`_OUTPUT_RECOVERY_REQUIRED` و`_DURABILITY_UNCONFIRMED`.

أبلِغ عن المشكلات عبر `NevercCoreAPI.EmitDiagnostic` مع
`NevercDiagnosticDescriptor` يحمل درجة الخطورة (`NOTE` أو `REMARK` أو `WARNING`
أو `ERROR` أو `FATAL`) والرمز ومعرِّف الإضافة ومعرِّف المرحلة والرسالة والملاحظات
وموقع المصدر والنطاقات والإصلاحات المقترحة. واستدعِ `CheckCancelled` قبل أي عمل
مُكلِف.

## الأمثلة

بناؤها جميعًا:

```sh
cmake --build build-neverc --target neverc-pluginsdk-examples
```

يُصرَّف كل مثال مرتين — مرة بمُصرِّف C المُضيف المُهيَّأ ومرة بـNeverC المبني حديثًا —
فتُثبَت الواجهة الثنائية من الجهتين. وتُوضع الوحدات في
`build-neverc/neverc/pluginsdk/examples/host/`.

| المثال | هدف CMake | ما يعرضه |
|---|---|---|
| [`DriverTracePlugin.c`] | `neverc-plugin-example-driver-trace` | تسجيل الخيارات، مراقبة المراحل، اعتراض المهام |
| [`VirtualHeaderPlugin.c`] | `neverc-plugin-example-virtual-header` | مزوِّد VFS يقدّم ترويسة من الذاكرة |
| [`ASTRewritePlugin.c`] | `neverc-plugin-example-ast-rewrite` | اعتراض المُحلِّل وتعديل الشجرة النحوية بشكل ذرّي |
| [`ExamplePlugin.c`] | `neverc-plugin-example-ir-overview` | مرور IR على مستوى الوحدة يجتاز قائمة الدوال بمؤشر قيم |
| [`FunctionPass.c`] | `neverc-plugin-example-function-pass` | مرور IR مستقر على مستوى الدالة |
| [`MachinePass.c`] | `neverc-plugin-example-machine-pass` | مرور MIR مستقر عند خطّاف ما قبل الإصدار |
| [`MCObserverPlugin.c`] | `neverc-plugin-example-mc-observer` | أحداث إصدار MC للقراءة فقط |
| [`ObjectRewritePlugin.c`] | `neverc-plugin-example-object-rewrite` | إعادة كتابة ObjectGraph معاملاتيًا |
| [`CustomCallConvPlugin.c`] | `neverc-plugin-example-custom-callconv` | اصطلاحات استدعاء مُوجَّهة بالبيانات |
| [`DynCodeTracePlugin.c`] | `neverc-plugin-example-dyncode-trace` | مراقبة خط أنابيب dyncode |
| [`DynCodeEncoderPlugin.c`] | `neverc-plugin-example-dyncode-encoder` | اعتراض ترميز مجموعة محارف dyncode |
| [`CrtShimPlugin.c`] | `neverc-plugin-example-crt-shim` | إضافة بلا أي اعتماد على CRT |
| [`BenchPlugin.c`] | `neverc-plugin-example-abi-bench` | قياس دقيق لإنتاجية استدعاءات الواجهة الثنائية |

تحميل أحدها:

```sh
neverc -fplugin=build-neverc/neverc/pluginsdk/examples/host/FunctionPass.so \
  -O2 -c input.c -o input.o
```

## المصادر المِعيارية

| الملف | ما يضمنه |
|---|---|
| [`neverc/include/neverc/Plugin/Schema/PhaseSchema.json`] | معرِّفات المراحل والسياسات والاستقرار وبوابات التدقيق |
| [`pluginsdk/manifest/plugin.json`] | إصدار الواجهة الثنائية، ومعرِّفات/إصدارات/استقرار الواجهات، وبصمات المخططات، والأهداف المدعومة |
| [`pluginsdk/abi/plugin.json`] | الحجم والمحاذاة وإزاحات الحقول المقيسة لكل بنية عامة، لكل مفتاح واجهة ثنائية للمُضيف |
| [`docs/plugin-api/coverage.json`] | يربط كل مرحلة مستقرة باختبارات إيجابية وسلبية واستبدالية ومراقِبة واختبارات البوابات المختومة |

وبذلك يمكن التحقق من حزمة تطوير مقابل مُضيف آليًا، ويمكن لبناء إضافة أن يؤكّد
ترتيب بناه مقابل مفتاح الواجهة الثنائية الذي ستُحمَّل فيه.

<!-- reference links -->
[`ASTRewritePlugin.c`]: ../../pluginsdk/examples/ASTRewritePlugin.c
[`BenchPlugin.c`]: ../../pluginsdk/examples/BenchPlugin.c
[`CrtShimPlugin.c`]: ../../pluginsdk/examples/CrtShimPlugin.c
[`CustomCallConvPlugin.c`]: ../../pluginsdk/examples/CustomCallConvPlugin.c
[`docs/plugin-api/coverage.json`]: coverage.json
[`DriverTracePlugin.c`]: ../../pluginsdk/examples/DriverTracePlugin.c
[`DynCodeEncoderPlugin.c`]: ../../pluginsdk/examples/DynCodeEncoderPlugin.c
[`DynCodeTracePlugin.c`]: ../../pluginsdk/examples/DynCodeTracePlugin.c
[`ExamplePlugin.c`]: ../../pluginsdk/examples/ExamplePlugin.c
[`FunctionPass.c`]: ../../pluginsdk/examples/FunctionPass.c
[`MachinePass.c`]: ../../pluginsdk/examples/MachinePass.c
[`MCObserverPlugin.c`]: ../../pluginsdk/examples/MCObserverPlugin.c
[`neverc/include/neverc/Plugin/Schema/PhaseSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PhaseSchema.json
[`ObjectRewritePlugin.c`]: ../../pluginsdk/examples/ObjectRewritePlugin.c
[`PluginAST.h`]: ../../neverc/include/neverc/Plugin/PluginAST.h
[`PluginCore.h`]: ../../neverc/include/neverc/Plugin/PluginCore.h
[`PluginDriver.h`]: ../../neverc/include/neverc/Plugin/PluginDriver.h
[`PluginDynCode.h`]: ../../neverc/include/neverc/Plugin/PluginDynCode.h
[`PluginIR.h`]: ../../neverc/include/neverc/Plugin/PluginIR.h
[`PluginLink.h`]: ../../neverc/include/neverc/Plugin/PluginLink.h
[`PluginLTO.h`]: ../../neverc/include/neverc/Plugin/PluginLTO.h
[`PluginMC.h`]: ../../neverc/include/neverc/Plugin/PluginMC.h
[`PluginMIR.h`]: ../../neverc/include/neverc/Plugin/PluginMIR.h
[`PluginObject.h`]: ../../neverc/include/neverc/Plugin/PluginObject.h
[`PluginPrep.h`]: ../../neverc/include/neverc/Plugin/PluginPrep.h
[`pluginsdk/abi/plugin.json`]: ../../pluginsdk/abi/plugin.json
[`pluginsdk/examples/DriverTracePlugin.c`]: ../../pluginsdk/examples/DriverTracePlugin.c
[`pluginsdk/manifest/plugin.json`]: ../../pluginsdk/manifest/plugin.json
[`pluginsdk/templates/minimal/Plugin.c`]: ../../pluginsdk/templates/minimal/Plugin.c
[`PluginSema.h`]: ../../neverc/include/neverc/Plugin/PluginSema.h
[`PluginSource.h`]: ../../neverc/include/neverc/Plugin/PluginSource.h
[`PluginTarget.h`]: ../../neverc/include/neverc/Plugin/PluginTarget.h
[`Schema/PhaseSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PhaseSchema.json
[`Schema/PluginPhaseSchema.inc`]: ../../neverc/include/neverc/Plugin/Schema/PluginPhaseSchema.inc
[`utils/plugin-api/check-global-state.py`]: ../../utils/plugin-api/check-global-state.py
[`VirtualHeaderPlugin.c`]: ../../pluginsdk/examples/VirtualHeaderPlugin.c

</div>
