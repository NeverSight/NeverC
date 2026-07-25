**اللغات**: [English](migration-from-prototype.md) | [简体中文](migration-from-prototype.zh-CN.md) | [繁體中文](migration-from-prototype.zh-TW.md) | [日本語](migration-from-prototype.ja.md) | [한국어](migration-from-prototype.ko.md) | [Français](migration-from-prototype.fr.md) | [Deutsch](migration-from-prototype.de.md) | [Español](migration-from-prototype.es.md) | [Italiano](migration-from-prototype.it.md) | [Русский](migration-from-prototype.ru.md) | [العربية](migration-from-prototype.ar.md)

# الانتقال من واجهة الإضافات الأولية

واجهة الإضافات الأولية غير المُصدَرة — نقطة دخولها `nevercGetPluginInfo`، وجدول
الدوال الافتراضية الوحيد `NevercHostAPI`، واستدعاءات `Register*Pass`، وخطاطيف
`NEVERC_INTERPOSE_*`، ومُحمِّل `-fplugin-pass=` — أُزيلت جميعها قبل أول إصدار
عام. أول واجهة ثنائية عامة هي واجهة الواصفات القائمة على المراحل الموثَّقة في
[README.md](README.md): تُصدِّر الإضافات الدالة `neverc_plugin_entry` وتتفاوض على
جداول قدرات مُصدَّرة بإصدارات مستقلة.

لا توجد طبقة توافق ولا انقسام بين `v1` و`v2`. أعِد ترجمة *الشيفرة المصدرية*
للإضافة مقابل الترويسات العامة؛ تربط هذه الصفحة كل بنية من بنى النموذج الأولي
ببديلها في الإصدار الأول، أو بتغيّر دلالي، أو بعدم ترحيل صريح.

## الملفات الثنائية الأولية مرفوضة

يفشل تحميل كائن مشترك أولي مع تشخيص ثابت:

```
plugin exports the removed 'nevercGetPluginInfo' prototype ABI; migrate it to
the first public descriptor ABI and export 'neverc_plugin_entry'
```

أما المكتبة التي لا تُصدِّر أيًّا من نقطتي الدخول فتفشل برسالة
`plugin has no 'neverc_plugin_entry' entry`. لا يُحمَّل أي شيء إلى أن تُنقل
الشيفرة المصدرية.

## نقطة الدخول

| النموذج الأولي | أول واجهة ثنائية عامة |
|---|---|
| `NevercPluginInfo nevercGetPluginInfo(void)` | `NevercStatus NEVERC_CALL neverc_plugin_entry(const NevercBootstrapAPI *Bootstrap, NevercPluginDescriptor *OutPlugin)` |

لم تعد نقطة الدخول *تُرجِع* بنية بالقيمة. بل تملأ واصفًا
`NevercPluginDescriptor` يوفّره المستدعي، مع احترام
`OutPlugin->Header.StructSize`، وتُرجِع `NevercStatus`. استعلم من `Bootstrap` عن
جداول القدرات التي تحتاجها قبل أن تعلن دعمها.

## حقول `NevercPluginInfo`

| حقل النموذج الأولي | ما يقابله في الإصدار الأول |
|---|---|
| `APIVersion` | `Descriptor.Header` (بنية `NevercABITableHeader` تحمل `StructSize` و`NEVERC_PLUGIN_ABI_MAJOR` و`NEVERC_PLUGIN_ABI_MINOR`) |
| `PluginName` | `Descriptor.DisplayName` (من نوع `NevercStringView`)، إضافة إلى `Descriptor.PluginID` ثابت بصيغة DNS معكوسة يُستخدم مفتاحًا لحالة كل نطاق |
| `PluginVersion` | `Descriptor.Version` (من نوع `NevercSemanticVersion`) |
| `RegisterPasses(API, Reg)` | `Descriptor.Register(Core, Registrar, RegistrarContext, ProcessState)`، إضافة إلى ردود نداء دورة الحياة `ProcessBegin` و`SessionBegin`/`SessionEnd` و`TaskBegin`/`TaskEnd` |
| `Destroy()` | `Descriptor.Destroy(Core, ProcessState)` |
| *(لا مقابل في النموذج الأولي)* | يجب التصريح بصدق عن `Descriptor.Concurrency` و`Descriptor.Reentrancy` (مثل `NEVERC_CONCURRENCY_SESSION_SERIAL` و`NEVERC_REENTRANCY_ALLOWED`) |

## الوصول إلى المضيف: جدول واحد ← جداول قدرات

كان النموذج الأولي يمرّر إلى كل رد نداء جدول `NevercHostAPI` واحدًا يضم أكثر من
200 مدخلة، ويحمي الحقول الجديدة بالماكرو `NEVERC_API_FN`. يستبدل الإصدار الأول
ذلك بجداول قدرات مُصدَّرة بإصدارات مستقلة، يُستعلَم عنها عند الحاجة:

```c
NevercInterfaceID Driver = { NEVERC_INTERFACE_DRIVER_HIGH,
                             NEVERC_INTERFACE_DRIVER_LOW };
const void *Table = NULL;
uint16_t Minor = 0;
uint64_t TableSize = 0;
NevercStatus S = Bootstrap->QueryInterface(
    Bootstrap->Context, Driver, NEVERC_DRIVER_API_MAJOR,
    NEVERC_DRIVER_API_MINOR, &Table, &Minor, &TableSize);
```

اشترط تطابق الإصدار الرئيسي، وتحقّق من `TableSize` باستخدام `offsetof` قبل قراءة
أي حقل. الواجهات مُقسَّمة حسب المجال: Core وDriver وSource وPrep وAST وSema وIR
وMIR وTarget وMC وObject وLink وLTO وDynCode.

## التسجيل: `Register*Pass` والخطاطيف ← مراقبون/معترضون/مزوّدون

كان التسجيل في النموذج الأولي يربط رد نداء بخطّاف:

```c
API->RegisterModulePass(Reg, NEVERC_INTERPOSE_PRE_OPT, myPass, ud, "my-pass");
```

أما الإصدار الأول فيسجّل، داخل `Register`، معالجًا مُنمَّطًا على مرحلة يحدّدها
مُعرِّف `NevercInterfaceID` بطول 128 بتًا:

| استدعاء النموذج الأولي | استدعاء المُسجِّل في الإصدار الأول |
|---|---|
| ممر للقراءة فقط | `Registrar->RegisterObserver(NevercObserverDescriptor)` مع النقطتين `NEVERC_OBSERVER_BEFORE`/`NEVERC_OBSERVER_AFTER` |
| ممر يغلّف مرحلة أو يقصرها | `Registrar->RegisterInterceptor(NevercInterceptorDescriptor)`؛ استدعِ `Continuation->InvokeNext` مرة واحدة على الأكثر واضبط `OutResult->Action` |
| ممر يستبدل تحويلًا مدمجًا | `Registrar->RegisterProvider(...)` على مرحلة سياستها `REPLACEABLE` |
| قراءة `-fplugin-pass-arg=` | `Registrar->RegisterOption(NevercOptionDescriptor)` للتصريح بخيار حقيقي للمُشغِّل |

يتحوّل «ممر الوحدة عند `PRE_OPT`» في النموذج الأولي إلى مراقب أو معترض أو مزوّد
على مرحلة IR المسماة `neverc.ir.pass.pre_opt`.

## ربط الخطاطيف بالمراحل

| خطّاف النموذج الأولي | مرحلة الإصدار الأول (الاسم) |
|---|---|
| `NEVERC_INTERPOSE_PRE_OPT` | `neverc.ir.pass.pre_opt` |
| `NEVERC_INTERPOSE_POST_OPT` | `neverc.ir.pass.post_opt` |
| `NEVERC_INTERPOSE_PIPELINE_START` | `neverc.ir.pass.pipeline_start` |
| `NEVERC_INTERPOSE_PIPELINE_LAST` | `neverc.ir.pass.optimizer_last` |
| `NEVERC_INTERPOSE_BEFORE_CODEGEN_PREEMIT` | `neverc.mir.pass.preemit` |
| `NEVERC_INTERPOSE_AFTER_CODEGEN_FINAL_MIR` | `neverc.mir.pass.final` |
| `NEVERC_INTERPOSE_LTO_PRE_OPT` / `LTO_POST_OPT` | مرحلتا LTO: `neverc.link.lto_resolve` / `neverc.link.lto_generate` (انظر [mir.md](mir.md)) |
| `NEVERC_INTERPOSE_LINK_PRE_LAYOUT` / `LINK_POST_LAYOUT` | المرحلة `neverc.link.layout` مُراقَبة عند `BEFORE` / `AFTER` |
| `NEVERC_INTERPOSE_LINK_POST_EMIT` | `neverc.link.post_emit` |
| `NEVERC_INTERPOSE_SC_*` (dyncode) | مراحل dyncode المُنمَّطة في [dyncode.md](dyncode.md) |

القائمة المعيارية لمعرّفات المراحل وسياساتها ومستويات استقرارها وبواباتها
التحقّقية موجودة في
`neverc/include/neverc/Plugin/Schema/PhaseSchema.json`؛ أما عقد التغطية القابل
للتنفيذ فهو [coverage.json](coverage.json). قد يقابل خطّافٌ كان نقطة واحدة
سابقًا أكثرَ من مُعرِّف مرحلة، لكلٍّ منها سياستها ودليلها.

## ردود نداء الممرات والمقابض وتحرير البايتات

| النموذج الأولي | الإصدار الأول |
|---|---|
| `NevercModulePassFn(NevercModuleRef, API, ud)` وما شابهها | تتلقى ردود النداء بنية `NevercPhaseFrame`؛ وكائنات IR/MIR/AST/الرسم البياني مقابض مُنمَّطة ومحدّدة النطاق ومُبهمة تُؤخذ من جدول القدرات المعني (انظر [ir.md](ir.md) و[mir.md](mir.md) و[ast-sema.md](ast-sema.md) و[target-mc-object.md](target-mc-object.md)) |
| النوع العام `NevercValueRef` | أُزيل لصالح مقابض IR المُنمَّطة |
| التعديل الموضعي لمرجع `Ref` حي | تمرّ جميع التغييرات عبر واجهات المضيف المعامَلاتية |
| `NevercBinaryPassFn(uint8_t **Data, uint64_t *Len, ...)` | أُزيلت؛ تستخدم تحريرات بايتات dyncode بانيَ الصور المُتحقَّق منه (read/write/insert/append/resize)، انظر [dyncode.md](dyncode.md) |

تظل المقابض والعروض المُستعارة صالحة داخل نطاق رد النداء فقط، تمامًا كما في
السابق؛ فلا تخزّنها بعد عودة رد النداء.

## طبقات التسهيل المُزالة

كان النموذج الأولي يحزم أدوات مساعدة عامة داخل جدول الدوال. هذه الأدوات **ليست**
جزءًا من أول واجهة ثنائية عامة:

| النموذج الأولي | الإصدار الأول |
|---|---|
| `ArenaCreate` / `StrMapCreate` / `IntMapCreate` / `StrBuilderCreate` / `ValueSetCreate` | لم تُرحَّل؛ استخدم `Core->Allocate`/`Core->Deallocate` مع حاوياتك الخاصة، أو واجهات المجال المُنمَّطة |
| ماكروات `NEVERC_FOR_EACH_*` / `NEVERC_COLLECT_*` | استُبدلت بالتكرار المُنمَّط في جدول قدرات كل مجال |
| `API->PluginGetArg` / `-fplugin-pass-arg=` | صرّح بالخيارات عبر `RegisterOption` واقرأها من خلال واجهة Driver |
| `DiagNoteF` / `DiagWarningF` / `DiagErrorF` | `Core->EmitDiagnostic(NevercDiagnosticDescriptor)` |

## التحميل وسطر الأوامر

| النموذج الأولي | الإصدار الأول |
|---|---|
| `-fplugin-pass=<path>` | `-fplugin=<path>` |
| `-fplugin-pass-arg=key=value` | صيغة الخيار التي تصرّح بها في `RegisterOption` (مثل `--driver-trace` أو `--my-opt=value`) |
| مُحمِّلان (`-fplugin` مقابل `-fplugin-pass`) | مُحمِّل واحد؛ تُسلَّم الوحدة إلى مُحمِّل واحد فقط |

## إدارة الإصدارات

اعتمد النموذج الأولي على جدول دوال واحد يتنامى باطّراد إضافة إلى حُرَّاس
`NEVERC_API_FN`. في الإصدار الأول يُصدَّر كل جدول قدرات على حِدة: اشترط تطابق
الإصدار الرئيسي، وتحقّق من `StructSize`/`TableSize` قبل قراءة حقل مُضاف. تُلحَق
الدوال الجديدة بالبادئة المستقرة للجدول ضمن الإصدار الرئيسي الأول من الواجهة
الثنائية، فتظل الإضافة المبنية على إصدار فرعي أقدم تعمل مع مضيف أحدث.

## مثال متكامل

يعرض الملف `pluginsdk/examples/DriverTracePlugin.c` الشكل الكامل للإصدار الأول:
واصف `neverc_plugin_entry`، ودورة الحياة `ProcessBegin`/`Session`/`Task`،
واستدعاء `RegisterOption` لراية حقيقية في سطر الأوامر، و`RegisterObserver` على
`neverc.driver.raw_arguments`، و`RegisterInterceptor` على
`neverc.driver.execute_job` يستدعي `InvokeNext` مرة واحدة بالضبط. أما
`pluginsdk/examples/ExamplePlugin.c` فيغطي مراحل IR وMIR وobject وlink.
