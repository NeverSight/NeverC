**اللغات**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# واجهة NeverC الثنائية للإضافات (Plugin ABI)

أول واجهة ثنائية عامة للإضافات في NeverC هي واجهة بلغة C خالصة قائمة على المراحل
(phases). الإضافة عبارة عن وحدة مشتركة تُصدِّر دالة واحدة فقط، وتتفاوض على جداول
قدرات مُرقَّمة بإصدارات، وتعمل داخل نطاقات صريحة هي Process و Session و Task. وهي
لا تُضمِّن أي ترويسة من LLVM، ولا ترتبط بالمُصرِّف، ولا تتبادل أي نوع C++ عبر الحدّ
الفاصل.

أما واجهة النموذج الأولي غير المُصدَرة ونقطة دخولها `nevercGetPluginInfo` فقد
**أُزيلت**. تُرفض ثنائيات النموذج الأولي مع تشخيص ترحيل؛ أعد ترجمة مصادرها مقابل
الترويسات العامة. راجع
[الترحيل من واجهة النموذج الأولي](migration-from-prototype.ar.md) للاطلاع على
جدول التقابل الكامل بين القديم والجديد.

## ابدأ من هنا

- [واجهة Source والإدخال/الإخراج](source.ar.md)
- [واجهة المعالج المسبق](prep.ar.md)
- [واجهة AST والتحليل الدلالي](ast-sema.ar.md)
- [واجهة IR](ir.ar.md)
- [واجهة MIR](mir.ar.md)
- [واجهات Target و MC والتجميع والملفات الكائنية](target-mc-object.ar.md)
- [واجهة DynCode](dyncode.ar.md)
- [اصطلاحات الاستدعاء المخصّصة](custom-callconv/README.ar.md)
- [الترحيل من واجهة النموذج الأولي](migration-from-prototype.ar.md)
- [أدلة تغطية المراحل](coverage.json)

## نموذج التنفيذ

يقود المضيف الإضافة عبر ثلاثة نطاقات متداخلة. يسلّم كل نطاق الإضافةَ مؤشرَ حالة
مبهمًا تُخصِّصه الإضافة وتملكه بنفسها، ولذلك لا تحتاج الإضافة المكتوبة بشكل صحيح إلى
أي حالة عامة قابلة للتغيير.

| النطاق | ردود النداء | المعنى |
|---|---|---|
| Process | `ProcessBegin`، `Register`، `Destroy` | عملية مُصرِّف واحدة. هنا تُستعلَم الواجهات وتُسجَّل القدرات. |
| Session | `SessionBegin`، `SessionEnd` | استدعاء واحد للمُشغِّل (driver). |
| Task | `TaskBegin`، `TaskEnd` | وحدة عمل واحدة، يُعرِّفها `NevercTaskKind`. |

أنواع المهام هي `INVOCATION` و `TRANSLATION_UNIT` و `LTO` و `LINK` و `CODEGEN`
و `OBJECT` و `DYNCODE`.

يستدعي المضيف `ProcessBegin` أولًا، ثم `Register` مرة واحدة بالضبط. التسجيل هو
الموضع الوحيد الذي يجوز فيه إضافة الخيارات والمراقبين والمعترِضين والمزوِّدين؛
وبعده يُجمَّد مخطط المراحل.

## المراحل

المرحلة هي انتقال مُسمّى ومُرقَّم بإصدار من أثر مُدخَل إلى أثر مُخرَج. يوفّر NeverC
**130 مرحلة مدمجة** موزّعة على مجالات المُشغِّل و source والمعالج المسبق والنحو
والدلالة و IR و codegen و MIR و MC والتجميع والملفات الكائنية والربط و dyncode،
إضافة إلى 8 عائلات من معرّفات التوسعة محجوزة للمراحل التي تعرّفها الإضافات.

تُعلن كل مرحلة عن سياستها، ولا يجوز للإضافة أن ترتبط بها إلا بالطرق التي تسمح بها
تلك السياسة:

| راية السياسة | ما يجوز للإضافة فعله |
|---|---|
| `NEVERC_PHASE_OBSERVABLE` | تسجيل مراقِب لتلقّي إشعار للقراءة فقط. |
| `NEVERC_PHASE_INTERCEPTABLE` | تغليف المرحلة وتقرير ما إذا كانت ستستدعي بقية السلسلة. |
| `NEVERC_PHASE_REPLACEABLE` | تسجيل مزوِّد يُنتج المُخرَج بنفسه. |
| `NEVERC_PHASE_SKIPPABLE_WITH_PROOF` | تخطّي الانتقال مع تقديم مقبض إثبات (proof). |
| `NEVERC_PHASE_SEALED_HOST_GATE` | لا شيء. المدقِّقات وعمليات الاعتماد ملك للمضيف، ولا يمكن استبدالها أو اعتراضها أو تخطّيها. |

تُسلَّم إشعارات المراقِبين عند النقاط التي تعلنها المرحلة:
`NEVERC_OBSERVER_BEFORE` و `NEVERC_OBSERVER_AFTER` و
`NEVERC_OBSERVER_AFTER_COMMIT`.

يتلقّى المعترِض كائن `NevercPhaseContinuation`. ويجب أن يستدعي `InvokeNext`
**مرة واحدة على الأكثر**، على خيط رد النداء نفسه، ثم يُبلّغ في
`NevercPhaseResult.Action` بإحدى القيم `NEVERC_PHASE_CONTINUE` أو
`NEVERC_PHASE_REPLACE` أو `NEVERC_PHASE_SKIP`.

المصدر المعياري لمعرّفات المراحل وسياساتها ومستويات استقرارها وبوابات التدقيق هو
`neverc/include/neverc/Plugin/Schema/PhaseSchema.json`. أما الملف المُولَّد
`PluginPhaseSchema.inc` فيكشفها كثوابت وقت الترجمة مثل
`NEVERC_PHASE_IR_PASS_PRE_OPT_HIGH` / `_LOW`.

## إضافة صغرى كاملة

هذا هو `pluginsdk/templates/minimal/Plugin.c`. يُحمَّل، ويتفاوض على الواجهة
الثنائية، ولا يسجّل شيئًا، ويُفرَّغ بنظافة — انسخ المجلد وابنِ عليه.

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
  /* سجّل هنا الخيارات أو المراقِبين أو المعترِضين أو المزوِّدين. */
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

`OutPlugin` هو مخزن مؤقت يملكه المُستدعي. عند الدخول يمثّل `Header.StructSize`
السعة القابلة للكتابة؛ تكتب الإضافة هذا العدد من البايتات على الأكثر، ثم تُبلّغ
بالحجم الذي أنتجته فعليًا.

## التفاوض على الواجهات

تُجلَب جداول القدرات بمعرّف واجهة بطول 128 بت، لا عبر الرموز. اطلب الإصدار الرئيسي
(major) الذي تُرجمت مقابله، وأدنى إصدار فرعي (minor) يمكنك العمل به:

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

إنّ فحص `TableSize` مقابل إزاحة آخر دالة تستدعيها هو القاعدة التي تجعل هذه الواجهة
الثنائية قابلة للتوسّع: يُلحق المضيف الأحدث حقولًا في النهاية، وتستمر الإضافة
الأقدم في العمل لأنها لا تقرأ أبدًا خارج البادئة التي تحقّقت منها. ويطبّق الماكرو
`NEVERC_ABI_FIELD_AVAILABLE(header, type, field)` الفحص نفسه على بنية تسلّمتها.

الواجهات العامة وترويساتها:

| الواجهة | الجدول | الترويسة |
|---|---|---|
| `NEVERC_INTERFACE_CORE` | `NevercCoreAPI` | `PluginCore.h` |
| `NEVERC_INTERFACE_DRIVER` | `NevercDriverAPI` | `PluginDriver.h` |
| `NEVERC_INTERFACE_IO`، `..._SOURCE_LOCATION` | `NevercIOAPI`، `NevercSourceLocationAPI` | `PluginSource.h` |
| `NEVERC_INTERFACE_PREP` | `NevercPrepAPI` | `PluginPrep.h` |
| `NEVERC_INTERFACE_AST`، `..._PARSER` | `NevercASTAPI`، `NevercParserAPI` | `PluginAST.h` |
| `NEVERC_INTERFACE_SEMA` | `NevercSemaAPI` | `PluginSema.h` |
| `NEVERC_INTERFACE_IR_CORE`، `..._BUILDER`، `..._ANALYSIS`، `..._PASS`، `..._GEN`، `..._OPTIMIZATION` | جداول IR | `PluginIR.h` |
| `NEVERC_INTERFACE_TARGET`، `..._TARGET_ABI`، `..._CALLING_CONVENTION` | جداول Target | `PluginTarget.h` |
| `NEVERC_INTERFACE_MIR`، `..._MIR_ANALYSIS`، `..._MIR_PASS`، `..._MIR_PROVIDER` | جداول MIR | `PluginMIR.h` |
| `NEVERC_INTERFACE_MC`، `..._MC_EMISSION`، `..._MC_PROVIDER`، `..._ASSEMBLY_PROVIDER` | جداول MC | `PluginMC.h` |
| `NEVERC_INTERFACE_OBJECT`، `..._OBJECT_FORMAT`، `..._OBJECT_PHASE` | جداول Object | `PluginObject.h` |
| `NEVERC_INTERFACE_LINK`، `..._LINK_REGISTRAR`، `..._LINK_PHASE` | جداول Link | `PluginLink.h` |
| `NEVERC_INTERFACE_LTO`، `..._LTO_REGISTRAR` | جداول LTO | `PluginLTO.h` |
| `NEVERC_INTERFACE_DYNCODE`، `..._DYNCODE_REGISTRAR`، `..._DYNCODE_PHASE` | جداول DynCode | `PluginDynCode.h` |

الواجهة إمّا STABLE (لا يجوز للمضيف الأحدث سوى الإلحاق) وإمّا LOCKSTEP (مخططات
خاصة بالهدف يجب أن تتطابق تمامًا). قارن بصمة المخطط (digest) قبل استهلاك قيم
LOCKSTEP.

## البناء

ضمِّن الترويسة المُجمِّعة، أو المجالات التي تستخدمها فقط:

```c
#include "neverc/Plugin/NevercPluginAPI.h"
```

ابنِ وحدة مشتركة باستخدام NeverC نفسه:

```sh
neverc --target=arm64-apple-macosx -shared \
  -I/path/to/pluginsdk/include \
  -o MyPlugin.dylib MyPlugin.c
```

أو باستخدام CMake مقابل حزمة SDK مثبَّتة:

```cmake
find_package(NevercPluginSDK REQUIRED)
add_library(my_plugin MODULE my_plugin.c)
target_link_libraries(my_plugin PRIVATE NevercPluginSDK::headers)
```

أو باستخدام pkg-config:

```sh
cc -shared $(pkg-config --cflags neverc-plugin) -o my_plugin.so my_plugin.c
```

استخدم `.so` أو `.dylib` أو `.dll` بحسب المضيف. لا ترتبط حزمة SDK بـ LLVM ولا
بزمن تشغيل NeverC — فـ `NevercPluginSDK::headers` هدف يتكوّن من ترويسات فقط.

## التحميل والضبط

```sh
neverc -fplugin=./MyPlugin.dylib -c input.c -o input.o
```

| الخيار | الصيغة | الغرض |
|---|---|---|
| `-fplugin=<path>` | قابل للتكرار | تحميل وحدة مشتركة لإضافة. |
| `-fplugin-arg=<plugin-id>:<key>=<value>` | قابل للتكرار | تمرير قيمة ضمن فضاء أسماء إلى خيار إضافة مُسجَّل. |
| `-fplugin-provider=<phase>:<plugin-id>` | قابل للتكرار | اختيار الإضافة التي تزوّد مرحلة قابلة للاستبدال. |

لا يجوز حذف المُحدِّد `<plugin-id>:` إلا عندما تكون إضافة واحدة بالضبط نشطة.
كما تُقبل الخيارات التي تسجّلها الإضافة عبر `RegisterOption` مباشرةً بهجائها
المُعلن، بصيغة راية أو ملتصقة أو منفصلة أو متعددة الوسائط. وتقديم وسائط إضافة أو
اختيار مزوِّد من دون `-fplugin=` خطأ صريح، لا تجاهل صامت.

## قواعد الواجهة الثنائية

- استعلم عن جداول القدرات عبر `QueryInterface`؛ واشترط تطابق الإصدار الرئيسي،
  وافحص `StructSize` قبل لمس أي حقل.
- هيّئ `Header` والمساحة المحجوزة في كل بنية عامة. صفّر البنية أولًا، ثم اضبط
  `StructSize` و `Major` و `Minor` و `Flags`.
- تعامل مع المقابض والعروض المُستعارة على أنها قيم مبهمة محدودة النطاق. لا تحتفظ
  أبدًا بمقبض على نطاق مهمة بعد انتهاء رد ندائه، ولا تستخدمه في جلسة أو مهمة
  أخرى، ولا تصطنع قيمة مقبض بنفسك.
- أعِد `NevercStatus` من كل رد نداء. ولا تدع استثناء C++ أو مؤشرًا يملكه المضيف
  يعبر حدّ لغة C.
- أعلن أضيق نموذجَي `NevercConcurrencyModel` (`SESSION_SERIAL`، `THREAD_SAFE`،
  `PROCESS_SERIAL`) و `NevercReentrancyModel` (`NONE`، `ALLOWED`) بشرط أن يكونا
  **صادقين**.
- نفّذ تعديلات IR و MIR و AST والمخططات والآثار عبر واجهات المضيف المعامَلاتية:
  ابدأ mutation، ثم جهّز التغييرات، ثم نفّذ commit أو abort. يتحقق الاعتماد
  وينشر بشكل ذرّي؛ والاعتماد الفاشل يترك الحالة السابقة كما هي.
- احتفظ بالحالة القابلة للتغيير داخل حالات process/session/task التي يوفّرها
  المضيف. أما الحالة العامة القابلة للتغيير فيفحصها
  `utils/plugin-api/check-global-state.py`.

تُلحق الدوال الجديدة بنهاية جداول قدرات مُرقَّمة بإصدارات مستقلة. ولا تتغير البادئة
المستقرة لأي جدول ضمن الإصدار الرئيسي الأول للواجهة الثنائية
(`NEVERC_PLUGIN_ABI_MAJOR` = 1).

## الحالة والتشخيصات

يحمل `NevercStatus` حقل `Code` و `Flags` وكلمة `Detail`. أشهر الرموز:

| الرمز | المعنى |
|---|---|
| `NEVERC_STATUS_OK` | نجاح. |
| `NEVERC_STATUS_INVALID_ARGUMENT` | مؤشر أو قيمة مطلوبة مفقودة أو مشوّهة. |
| `NEVERC_STATUS_ABI_MISMATCH` | الجدول المُتفاوَض عليه أصغر من اللازم أو الإصدار الرئيسي مختلف. |
| `NEVERC_STATUS_MISSING_INTERFACE` / `CAPABILITY_UNAVAILABLE` | المضيف لا يوفّر القدرة المطلوبة. |
| `NEVERC_STATUS_STALE_HANDLE` / `WRONG_SESSION` / `WRONG_SCOPE` / `WRONG_TYPE` | استُخدم مقبض خارج نطاق صلاحيته. |
| `NEVERC_STATUS_POLICY_VIOLATION` | سياسة المرحلة لا تسمح بهذه العملية. |
| `NEVERC_STATUS_VERIFICATION_FAILED` | رفض مدقِّق مختوم لدى المضيف الناتج. |
| `NEVERC_STATUS_CANCELLED` / `BUSY` / `RESOURCE_EXHAUSTED` | إلغاء تعاوني أو حدود موارد. |

أما بتات الرايات (`RECOVERABLE` و `OUTPUT_ALREADY_COMMITTED` و
`OUTPUT_MAY_BE_PARTIAL` و `OUTPUT_RECOVERY_REQUIRED` و
`DURABILITY_UNCONFIRMED`) فتصف ما جرى للمُخرَج، وهو بالضبط ما يحتاجه نظام البناء
ليقرّر ما إذا كانت إعادة المحاولة آمنة.

أبلغ عن المشكلات عبر `NevercCoreAPI.EmitDiagnostic` مع
`NevercDiagnosticDescriptor` الذي يحمل درجة الخطورة والرمز ومعرّف الإضافة ومعرّف
المرحلة والرسالة والملاحظات وموضع المصدر والنطاقات واقتراحات الإصلاح. واستدعِ
`CheckCancelled` قبل أي عمل مكلف.

## الأمثلة

ابنِ جميع الأمثلة:

```sh
cmake --build build-neverc --target neverc-pluginsdk-examples
```

يُترجَم كل مثال مرتين — مرة بمُصرِّف C المضيف المُهيَّأ، ومرة بـ NeverC المبني
حديثًا — فتثبت الواجهة الثنائية من الجهتين. وتظهر الوحدات في
`build-neverc/neverc/pluginsdk/examples/host/`.

| المثال | هدف CMake | ما يوضّحه |
|---|---|---|
| `DriverTracePlugin.c` | `neverc-plugin-example-driver-trace` | تسجيل الخيارات ومراقبة المراحل واعتراض المهام |
| `VirtualHeaderPlugin.c` | `neverc-plugin-example-virtual-header` | مزوِّد VFS يقدّم ترويسة من الذاكرة |
| `ASTRewritePlugin.c` | `neverc-plugin-example-ast-rewrite` | اعتراض المحلّل النحوي وتعديل AST بشكل ذرّي |
| `ExamplePlugin.c` | `neverc-plugin-example-ir-overview` | تمريرة IR على مستوى الوحدة تتنقّل في قائمة الدوال عبر مؤشّر قيم |
| `FunctionPass.c` | `neverc-plugin-example-function-pass` | تمريرة IR مستقرة على مستوى الدالة |
| `MachinePass.c` | `neverc-plugin-example-machine-pass` | تمريرة MIR مستقرة عند خطّاف pre-emit |
| `MCObserverPlugin.c` | `neverc-plugin-example-mc-observer` | أحداث إصدار MC للقراءة فقط |
| `ObjectRewritePlugin.c` | `neverc-plugin-example-object-rewrite` | إعادة كتابة ObjectGraph بأسلوب معامَلاتي |
| `CustomCallConvPlugin.c` | `neverc-plugin-example-custom-callconv` | اصطلاحات استدعاء مقودة بالبيانات |
| `DynCodeTracePlugin.c` | `neverc-plugin-example-dyncode-trace` | مراقبة خط أنابيب dyncode |
| `DynCodeEncoderPlugin.c` | `neverc-plugin-example-dyncode-encoder` | اعتراض ترميز مجموعة محارف dyncode |
| `CrtShimPlugin.c` | `neverc-plugin-example-crt-shim` | إضافة بلا أي اعتماد على CRT |
| `BenchPlugin.c` | `neverc-plugin-example-abi-bench` | قياس أداء مصغّر لإنتاجية استدعاءات الواجهة الثنائية |

حمِّل أحدها:

```sh
neverc -fplugin=build-neverc/neverc/pluginsdk/examples/host/FunctionPass.so \
  -O2 -c input.c -o input.o
```

## المصادر المعيارية

| الملف | ما يضمنه |
|---|---|
| `neverc/include/neverc/Plugin/Schema/PhaseSchema.json` | معرّفات المراحل وسياساتها واستقرارها وبوابات التدقيق |
| `pluginsdk/manifest/plugin.json` | إصدار الواجهة الثنائية، ومعرّفات/إصدارات/استقرار الواجهات، وبصمات المخططات، والأهداف المدعومة |
| `pluginsdk/abi/plugin.json` | الحجم والمحاذاة وإزاحات الحقول المقيسة لكل بنية عامة، بحسب مفتاح الواجهة الثنائية للمضيف |
| `docs/plugin-api/coverage.json` | يربط كل مرحلة مستقرة باختبارات إيجابية وسلبية واختبارات استبدال ومراقبة وبوابات مختومة |

وبذلك يمكن التحقق آليًا من مطابقة حزمة SDK لمضيف معيّن، كما يمكن لبناء الإضافة أن
يؤكّد تخطيط بنياته مقابل مفتاح الواجهة الثنائية الذي ستُحمَّل فيه.
