**اللغات**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

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

هذا التوقيع، المُعلَن في `PluginCore.h`، هو عقد الربط بأكمله. كل ما عداه — قراءة
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

الملف `Schema/PhaseSchema.json` هو المصدر المِعياري لمعرِّفات المراحل والسياسات
ومستويات الاستقرار وبوابات التدقيق. والملف المُولَّد
`Schema/PluginPhaseSchema.inc` يكشف كلًّا منها كثابت وقت التصريف — للمرحلة
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

هذا هو `pluginsdk/templates/minimal/Plugin.c` حرفيًا. يُحمَّل، ويتفاوض على الواجهة
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
| `NEVERC_INTERFACE_CORE_{HIGH,LOW}` | `NevercCoreAPI` | `PluginCore.h` |
| `NEVERC_INTERFACE_DRIVER_*` | `NevercDriverAPI` | `PluginDriver.h` |
| `NEVERC_INTERFACE_IO_*`، `..._SOURCE_LOCATION_*` | `NevercIOAPI`، `NevercSourceLocationAPI` | `PluginSource.h` |
| `NEVERC_INTERFACE_PREP_*` | `NevercPrepAPI` | `PluginPrep.h` |
| `NEVERC_INTERFACE_AST_*`، `..._PARSER_*` | `NevercASTAPI`، `NevercParserAPI` | `PluginAST.h` |
| `NEVERC_INTERFACE_SEMA_*` | `NevercSemaAPI` | `PluginSema.h` |
| `NEVERC_INTERFACE_IR_CORE_*`، `..._IR_BUILDER_*`، `..._IR_ANALYSIS_*`، `..._IR_PASS_*`، `..._IR_GEN_*`، `..._IR_OPTIMIZATION_*` | ستة جداول IR | `PluginIR.h` |
| `NEVERC_INTERFACE_TARGET_*`، `..._TARGET_ABI_*`، `..._CALLING_CONVENTION_*` | `NevercTargetAPI`، `NevercTargetABIAPI`، `NevercCallingConventionAPI` | `PluginTarget.h` |
| `NEVERC_INTERFACE_MIR_*`، `..._MIR_ANALYSIS_*`، `..._MIR_PASS_*`، `..._MIR_PROVIDER_*` | أربعة جداول MIR | `PluginMIR.h` |
| `NEVERC_INTERFACE_MC_*`، `..._MC_EMISSION_*`، `..._MC_PROVIDER_*`، `..._ASSEMBLY_PROVIDER_*` | أربعة جداول MC | `PluginMC.h` |
| `NEVERC_INTERFACE_OBJECT_*`، `..._OBJECT_FORMAT_*`، `..._OBJECT_PHASE_*` | ثلاثة جداول كائنات | `PluginObject.h` |
| `NEVERC_INTERFACE_LINK_*`، `..._LINK_REGISTRAR_*`، `..._LINK_PHASE_*` | ثلاثة جداول ربط | `PluginLink.h` |
| `NEVERC_INTERFACE_LTO_*`، `..._LTO_REGISTRAR_*` | `NevercLTOAPI`، `NevercLTORegistrarAPI` | `PluginLTO.h` |
| `NEVERC_INTERFACE_DYNCODE_*`، `..._DYNCODE_REGISTRAR_*`، `..._DYNCODE_PHASE_*` | ثلاثة جداول dyncode | `PluginDynCode.h` |

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

ومُسجِّلات المجالات — `NevercIRPassAPI.RegisterPass` و
`NevercTargetAPI.RegisterTarget` و`NevercObjectFormatAPI.RegisterFormat` وغيرها
— تأخذ جميعها السياق `RegistrarContext` نفسه وسيطًا ثانيًا، وبه يَنسِب المُضيف
التسجيل إلى إضافتك.

ويُعلن المزوِّد إضافةً إلى ذلك عقد الحتمية الخاص به، وهو ما تعتمد عليه ذاكرة البناء
المؤقتة:

```c
Provider.ProviderID    = SV("com.example.my-lowering");
Provider.Route         = /* triple / CPU / features / object format */;
Provider.Deterministic = NEVERC_TRUE;
Provider.Cacheable     = NEVERC_TRUE;
Provider.FallbackSafe  = NEVERC_FALSE;  /* built-in cannot silently take over */
```

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
  والحالة العامة القابلة للتغيير يفحصها `utils/plugin-api/check-global-state.py`.

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
| `DriverTracePlugin.c` | `neverc-plugin-example-driver-trace` | تسجيل الخيارات، مراقبة المراحل، اعتراض المهام |
| `VirtualHeaderPlugin.c` | `neverc-plugin-example-virtual-header` | مزوِّد VFS يقدّم ترويسة من الذاكرة |
| `ASTRewritePlugin.c` | `neverc-plugin-example-ast-rewrite` | اعتراض المُحلِّل وتعديل الشجرة النحوية بشكل ذرّي |
| `ExamplePlugin.c` | `neverc-plugin-example-ir-overview` | مرور IR على مستوى الوحدة يجتاز قائمة الدوال بمؤشر قيم |
| `FunctionPass.c` | `neverc-plugin-example-function-pass` | مرور IR مستقر على مستوى الدالة |
| `MachinePass.c` | `neverc-plugin-example-machine-pass` | مرور MIR مستقر عند خطّاف ما قبل الإصدار |
| `MCObserverPlugin.c` | `neverc-plugin-example-mc-observer` | أحداث إصدار MC للقراءة فقط |
| `ObjectRewritePlugin.c` | `neverc-plugin-example-object-rewrite` | إعادة كتابة ObjectGraph معاملاتيًا |
| `CustomCallConvPlugin.c` | `neverc-plugin-example-custom-callconv` | اصطلاحات استدعاء مُوجَّهة بالبيانات |
| `DynCodeTracePlugin.c` | `neverc-plugin-example-dyncode-trace` | مراقبة خط أنابيب dyncode |
| `DynCodeEncoderPlugin.c` | `neverc-plugin-example-dyncode-encoder` | اعتراض ترميز مجموعة محارف dyncode |
| `CrtShimPlugin.c` | `neverc-plugin-example-crt-shim` | إضافة بلا أي اعتماد على CRT |
| `BenchPlugin.c` | `neverc-plugin-example-abi-bench` | قياس دقيق لإنتاجية استدعاءات الواجهة الثنائية |

تحميل أحدها:

```sh
neverc -fplugin=build-neverc/neverc/pluginsdk/examples/host/FunctionPass.so \
  -O2 -c input.c -o input.o
```

## المصادر المِعيارية

| الملف | ما يضمنه |
|---|---|
| `neverc/include/neverc/Plugin/Schema/PhaseSchema.json` | معرِّفات المراحل والسياسات والاستقرار وبوابات التدقيق |
| `pluginsdk/manifest/plugin.json` | إصدار الواجهة الثنائية، ومعرِّفات/إصدارات/استقرار الواجهات، وبصمات المخططات، والأهداف المدعومة |
| `pluginsdk/abi/plugin.json` | الحجم والمحاذاة وإزاحات الحقول المقيسة لكل بنية عامة، لكل مفتاح واجهة ثنائية للمُضيف |
| `docs/plugin-api/coverage.json` | يربط كل مرحلة مستقرة باختبارات إيجابية وسلبية واستبدالية ومراقِبة واختبارات البوابات المختومة |

وبذلك يمكن التحقق من حزمة تطوير مقابل مُضيف آليًا، ويمكن لبناء إضافة أن يؤكّد
ترتيب بناه مقابل مفتاح الواجهة الثنائية الذي ستُحمَّل فيه.
