**اللغات**: [English](driver.md) | [简体中文](driver.zh-CN.md) | [繁體中文](driver.zh-TW.md) | [日本語](driver.ja.md) | [한국어](driver.ko.md) | [Français](driver.fr.md) | [Deutsch](driver.de.md) | [Español](driver.es.md) | [Italiano](driver.it.md) | [Русский](driver.ru.md) | [العربية](driver.ar.md)

[← واجهة NeverC الثنائية للإضافات](README.ar.md)

# واجهة Driver لإضافات NeverC

يحوّل المشغّل (driver) سطر الأوامر إلى مجموعة من المهام المنفَّذة. تكشف
`PluginDriver.h` هذا المسار في صورة ست مراحل وجدول قدرات واحد هو
`NevercDriverAPI`، فتستطيع الإضافة إعادة كتابة الوسائط، واختيار سلسلة الأدوات،
وإعادة هيكلة مخطط الإجراءات، وإضافة المهام أو استبدالها، بل وحتى تنفيذ مهمة
داخل العملية الحالية بدل إطلاق عملية جديدة.

## الواجهة

```c
#include "neverc/Plugin/PluginDriver.h"

Bootstrap->QueryInterface(
    Bootstrap->Context,
    (NevercInterfaceID){NEVERC_INTERFACE_DRIVER_HIGH,
                        NEVERC_INTERFACE_DRIVER_LOW},
    NEVERC_DRIVER_API_MAJOR, NEVERC_DRIVER_API_MINOR,
    &Table, &Minor, &TableSize);
```

`NevercDriverAPI` جدول مسطّح واحد يضم 67 فتحة دالة موزَّعة على خمس مناطق:
الوسائط الخام، والخيارات المُحلَّلة، واختيار سلسلة الأدوات، ومخطط الإجراءات،
ومخطط المهام. تحقَّق من `TableSize` مقابل إزاحة آخر فتحة تستعملها؛ الذيل الحالي
هو `GetJobResult`.

## مراحل المشغّل الست

| المرحلة | السياسة | الدخل ← الخرج |
|---|---|---|
| `neverc.driver.raw_arguments` | OBSERVABLE، INTERCEPTABLE | argv ← argv |
| `neverc.driver.parsed_arguments` | OBSERVABLE، INTERCEPTABLE | قائمة خيارات محلَّلة ← قائمة خيارات محلَّلة |
| `neverc.driver.select_toolchain` | إضافةً إلى REPLACEABLE | طلب سلسلة أدوات ← اختيار سلسلة أدوات |
| `neverc.driver.build_actions` | إضافةً إلى REPLACEABLE | طلب ← مخطط إجراءات |
| `neverc.driver.build_jobs` | إضافةً إلى REPLACEABLE | مخطط إجراءات ← مخطط مهام |
| `neverc.driver.execute_job` | إضافةً إلى REPLACEABLE | طلب تنفيذ مهمة ← نتيجة مهمة |

تتبع وحدات الماكرو الخاصة بها النمط المعتاد:
`NEVERC_PHASE_DRIVER_RAW_ARGUMENTS_{NAME,HIGH,LOW,POLICY,…}`.

## تسجيل خيار

تُعلَن الخيارات مرة واحدة أثناء `Register`، وبعدها يقبلها المشغّل على سطر
الأوامر تمامًا كما لو كانت مدمجة.

```c
typedef struct NevercOptionDescriptor {
  NevercABITableHeader Header;
  NevercStringView Spelling;
  NevercStringList Aliases;
  NevercOptionForm Form;                  /* FLAG, JOINED, SEPARATE, MULTI_ARG */
  NevercOptionValueType ValueType;        /* BOOL, INT, UINT, STRING, ENUM, PATH */
  NevercOptionMultiplicity Multiplicity;  /* SINGLE, LAST_WINS, APPEND */
  uint32_t ArgumentCount;
  NevercBool Required;
  NevercBool Hidden;
  NevercStringView Help;
  NevercStringView Metavar;
  NevercStructArrayView EnumValues;       /* NevercOptionEnumValue[] */
  NevercStringList Conflicts;
  NevercStringList Requires;
  NevercStringView TargetPredicate;
  NevercOptionValidatorFn Validator;
  void *UserData;
  NevercDestroyUserDataFn DestroyUserData;
} NevercOptionDescriptor;
```

من `pluginsdk/examples/DriverTracePlugin.c`:

```c
NevercOptionDescriptor Option = {0};
Option.Header = (NevercABITableHeader){sizeof(Option), NEVERC_DRIVER_API_MAJOR,
                                       NEVERC_DRIVER_API_MINOR, 0};
Option.Spelling     = SV("--driver-trace");
Option.Form         = NEVERC_OPTION_FLAG;
Option.ValueType    = NEVERC_OPTION_BOOL;
Option.Multiplicity = NEVERC_OPTION_SINGLE;
Option.Help         = SV("enable the driver trace example plugin");
Status = Registrar->RegisterOption(RegistrarContext, &Option);
```

تُستدعى `Validator` عند كل ظهور مع `NevercOptionValidationContext` يحمل معرِّف
الإضافة، والهجاء، وثلاثي الهدف، ورقم الظهور، فيمكن رفض قيمة بتشخيص حقيقي بدل
الفشل لاحقًا. ويقصر `TargetPredicate` الخيار على الثلاثيات المطابقة. تُقرأ القيم
مرة أخرى عبر `NevercCoreAPI.GetPluginOptionValueCount` و`GetPluginOptionValue`.

## الوسائط الخام

عند `neverc.driver.raw_arguments` يكون الأثر هو متجه argv. القراءة قائمة على
الفهرس، وكل مدخل يُبلِّغ عن مصدره:

```c
Driver->GetArgumentCount(Driver->Context, Frame, Frame->Input, &Count);

NevercStringView Value, Source;
NevercArgumentOrigin Origin;   /* COMMAND_LINE, CONFIGURATION, PLUGIN */
uint64_t Position;
Driver->GetArgument(Driver->Context, Frame, Frame->Input, Index,
                    &Value, &Origin, &Source, &Position);
```

التحرير معاملاتي ولا يجوز إلا من داخل مُعترِض، لأن التعديل مرتبط بالمتابعة
(continuation):

```c
NevercArgumentMutationHandle Mutation;
Driver->BeginArgumentMutation(Driver->Context, Frame, Continuation,
                              Frame->Input, &Mutation);
Driver->InsertArgument(Driver->Context, Mutation, Index, SV("-O2"));
Driver->ReplaceArgument(Driver->Context, Mutation, Index, SV("-O3"));
Driver->EraseArgument(Driver->Context, Mutation, Index);
Driver->CommitArgumentMutation(Driver->Context, Mutation);  /* أو Abort */
```

## الوسائط المُحلَّلة

تعمل `neverc.driver.parsed_arguments` على ظهورات الخيارات لا على السلاسل، وهو
ما تحتاجه عند إضافة راية يجب ألّا يُعاد تحليلها معجميًّا:

```c
typedef struct NevercOptionOccurrence {
  NevercABITableHeader Header;
  uint64_t Occurrence;
  NevercStringView Spelling;
  NevercStringList Values;
  NevercArgumentOrigin Origin;
  uint32_t Reserved;
} NevercOptionOccurrence;
```

تقرأ `GetOptionOccurrenceCount` و`GetOptionOccurrence`؛ ثم تُحرِّر
`BeginParsedArgumentMutation` و`AddOptionOccurrence` و
`RemoveOptionOccurrence` و`ReplaceOptionOccurrence` و
`CommitParsedArgumentMutation` / `AbortParsedArgumentMutation`.

## اختيار سلسلة الأدوات

يصف الطلب ما طُلب وما حسبه المشغّل:

```c
typedef struct NevercToolChainRequest {
  NevercABITableHeader Header;
  NevercStringView RequestedTriple;
  NevercStringView ComputedTriple;
  NevercStringView SysRoot;
  NevercStringView ResourceDir;
  NevercStringView CPU;
  NevercStringList Features;
  NevercExecutionLevel ExecutionLevel;  /* UNSPECIFIED, USER, KERNEL */
  NevercBool DynamicCodeProfile;
  uint32_t Reserved;
} NevercToolChainRequest;
```

يستطيع المُعترِض تعديل الطلب عبر `BeginToolChainMutation` و
`SetToolChainTriple` و`SetToolChainCPU` و`SetToolChainFeatures` و
`CommitToolChainMutation`. أما المزوِّد فيجيب عن المرحلة كاملة عبر
`CreateToolChainSelection`، مسمِّيًا أحد معرِّفات سلاسل الأدوات المدمجة أو
معرِّفه الخاص:

```c
NEVERC_TOOLCHAIN_ID_DARWIN        /* "neverc.builtin.darwin"      */
NEVERC_TOOLCHAIN_ID_LINUX         /* "neverc.builtin.linux"       */
NEVERC_TOOLCHAIN_ID_MSVC          /* "neverc.builtin.msvc"        */
NEVERC_TOOLCHAIN_ID_GENERIC_ELF   /* "neverc.builtin.generic-elf" */
NEVERC_TOOLCHAIN_ID_MACHO         /* "neverc.builtin.macho"       */
NEVERC_TOOLCHAIN_ID_GENERIC_GCC   /* "neverc.builtin.generic-gcc" */
```

تقرأ `GetToolChainSelection` النتيجة وتُبلِّغ عن `BuiltinProviderUsed`، فيعرف
المراقب ما إذا كانت إضافة قد فازت بالمرحلة.

## مخطط الإجراءات

عقدة الإجراء خطوة ترجمة ذات نوع. وتشير العقد إلى مدخلات المشغّل وإلى عقد أخرى:

```c
typedef struct NevercActionNode {
  NevercABITableHeader Header;
  NevercActionNodeID Node;
  NevercActionKind Kind;
  NevercDriverType OutputType;
  uint64_t InputCount;
  NevercDriverInputID DriverInput;
  NevercStringView BindArch;
  uint64_t Reserved;
} NevercActionNode;
```

| `NevercActionKind` | | `NevercDriverType` | |
|---|---|---|---|
| `INPUT` | `BIND_ARCH` | `PP_C`، `C`، `C_HEADER` | `PP_ASM`، `ASM` |
| `PREPROCESS` | `COMPILE` | `LLVM_IR`، `LLVM_BC` | `LTO_IR`، `LTO_BC` |
| `BACKEND` | `ASSEMBLE` | `OBJECT`، `IMAGE` | `DSYM` |
| `LINK`، `LIPO` | `DSYMUTIL` | `DEPENDENCIES` | `NOTHING` |
| `STATIC_LIB` | `DYNCODE` | | |

القراءة عبر `GetDriverInputCount` / `GetDriverInput`، و`GetActionNodeCount` /
`GetActionNode` / `GetActionNodeInput`، و`GetActionRootCount` /
`GetActionRoot`.

يمر بناء مخطط بديل عبر باني، ثم نشر واحد:

```c
NevercActionGraphBuilderHandle Builder;
Driver->CreateActionGraphBuilder(Driver->Context, Frame, Request, &Builder);

NevercActionNodeDescriptor Node = {0};
Node.Header     = (NevercABITableHeader){sizeof(Node), NEVERC_DRIVER_API_MAJOR,
                                         NEVERC_DRIVER_API_MINOR, 0};
Node.Kind       = NEVERC_ACTION_COMPILE;
Node.OutputType = NEVERC_DRIVER_TYPE_OBJECT;
Node.Inputs     = /* NevercActionNodeIDList */;
NevercActionNodeID Created;
Driver->AddActionNode(Driver->Context, Builder, &Node, &Created);

Driver->SetActionRoots(Driver->Context, Builder, Roots);
Driver->PublishActionGraph(Driver->Context, Frame, Builder, &OutGraph);
```

وتُحرِّر `RemoveActionNode` و`ReplaceActionNodeInputs` و
`SetActionNodeOutputType` و`SetActionNodeBindArch` بانيًا قيد الإنشاء. ولتعديل
مخطط المضيف القائم بدل إعادة بنائه، استعمل `BeginActionGraphMutation` و
`CommitActionGraphMutation`؛ وتتخلص `AbortActionGraphEdit` من أي من الشكلين.

## مخطط المهام

المهمة أمر يُنفَّذ. ويصف `NevercJobDescriptor` واحدة منها:

```c
typedef struct NevercJobDescriptor {
  NevercABITableHeader Header;
  NevercJobKind Kind;                             /* COMMAND, FRONTEND, LINKER,
                                                     ARCHIVE, PLUGIN, DYNCODE  */
  NevercResponseFileKind ResponseFileKind;        /* NONE, FULL, LIST          */
  NevercResponseFileEncoding ResponseFileEncoding;/* UTF8, CURRENT_CODE_PAGE,
                                                     UTF16                     */
  NevercBool InProcess;
  NevercActionNodeID SourceAction;
  NevercLinkerFlavor LinkerFlavor;                /* NONE, GNU, WIN_LINK, DARWIN */
  uint32_t Reserved;
  NevercStringView Executable;
  NevercStringList Arguments;
  NevercStringList Environment;
  NevercJobFileList Inputs;
  NevercJobFileList Outputs;
  NevercJobIDList Dependencies;
  NevercStringView CallbackID;
  NevercPluginJobCallbackFn Callback;
  void *UserData;
} NevercJobDescriptor;
```

اضبط `Kind` على `NEVERC_JOB_PLUGIN` مع `Callback`، فيشغّل المشغّل دالتك في
الموضع الذي كان سيطلق فيه عملية:

```c
static NevercStatus NEVERC_CALL run_job(const NevercPluginJobContext *Context,
                                        int32_t *OutExitCode, void *UserData) {
  /* Context->Arguments و->Environment و->Inputs و->Outputs مُستعارة. */
  *OutExitCode = 0;
  return neverc_status_ok();
}
```

قراءة المخطط تماثل مخطط الإجراءات: `GetJobCount` / `GetJob`، و
`GetJobDependency`، و`GetJobArgument` / `GetJobEnvironment`، و`GetJobInput` /
`GetJobOutput`. لاحظ أن `NevercJob` يُبلِّغ عن الأعداد فقط؛ اجلب كل سلسلة أو ملف
بالفهرس بدل توقّع مصفوفة مضمَّنة.

يستعمل التحرير `CreateJobGraphBuilder` أو `BeginJobGraphMutation`، ثم `AddJob`
و`RemoveJob` و`MoveJobBefore` و`ReplaceJob` و`SetJobArgument` و
`SetJobEnvironment` و`SetJobInput` و`SetJobOutput` و
`ReplaceJobDependencies`. والنشر بـ`PublishJobGraph` أو
`CommitJobGraphMutation`؛ والتخلص بـ`AbortJobGraphEdit`.

## تنفيذ مهمة

عند `neverc.driver.execute_job` يكون أثر الدخل هو
`NevercJobExecutionRequest`: المهمة مع قوائمها المُجسَّدة بالكامل من الوسائط
والبيئة والمدخلات والمخرجات والاعتماديات. ينفِّذ المزوِّد المهمة ويُبلِّغ عن
النتيجة:

```c
typedef struct NevercJobResultDescriptor {
  NevercABITableHeader Header;
  int32_t ExitCode;
  NevercBool ExecutionFailed;
  NevercBool HasProcessStatistics;
  uint32_t Reserved;
  NevercStringView ErrorMessage;
  NevercOutputSealList OutputSeals;
  uint64_t TotalTimeMicroseconds;
  uint64_t UserTimeMicroseconds;
  uint64_t PeakMemoryKiB;
} NevercJobResultDescriptor;
```

يحمل `OutputSeals` مقابض `NevercOutputSealHandle` المنتَجة عبر واجهة الإدخال
والإخراج (انظر [المصدر والإدخال/الإخراج](source.ar.md))، وبها يتأكد المضيف من
أن الملفات التي ادّعت مهمةٌ كتابتها موجودة فعلًا بالبصمات المُبلَّغ عنها. وتقرأ
`GetJobResult` نتيجة مُثبَّتة، وتُبلِّغ مثل اختيار سلسلة الأدوات عن
`BuiltinProviderUsed`.

## مثال متكامل: مراقبة الوسائط واعتراض تنفيذ المهمة

مُختصَر من `pluginsdk/examples/DriverTracePlugin.c`. لا تحتفظ الإضافة بأي
متغيرات عامة: حالة العملية تحمل الجداول المُتفاوَض عليها، أما عدّادات الجلسة
والمَهمة فتُجلب من المضيف داخل كل نداء راجع.

```c
static NevercStatus NEVERC_CALL
observe_arguments(const NevercPhaseFrame *Frame, NevercObserverPoint Point,
                  void *UserData) {
  DriverTraceProcessState *Process = (DriverTraceProcessState *)UserData;
  DriverTraceSessionState *Session = NULL;
  uint64_t ArgumentCount = 0;
  NevercStatus Status;
  if (Frame == NULL || Process == NULL)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  Status = Process->Core->GetSessionState(Process->Core->Context,
                                          Frame->Session, plugin_id(),
                                          (void **)&Session);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  Status = Process->Driver->GetArgumentCount(Process->Driver->Context, Frame,
                                             Frame->Input, &ArgumentCount);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;
  ++Session->ArgumentCallbacks;
  if (Point == NEVERC_OBSERVER_BEFORE && !Session->Announced) {
    Session->Announced = NEVERC_TRUE;
    return emit_trace_remark(Process, Frame, "driver argument phase observed",
                             30, 1001);
  }
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL
intercept_job(const NevercPhaseFrame *Frame,
              NevercPhaseContinuation *Continuation,
              NevercPhaseResult *OutResult, void *UserData) {
  DriverTraceProcessState *Process = (DriverTraceProcessState *)UserData;
  NevercJobExecutionRequest Request = {0};
  NevercPhaseResult Downstream = {0};
  NevercStatus Status;
  if (Frame == NULL || Continuation == NULL || OutResult == NULL || !Process)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  Request.Header = (NevercABITableHeader){
      sizeof(Request), NEVERC_DRIVER_API_MAJOR, NEVERC_DRIVER_API_MINOR, 0};
  Status = Process->Driver->GetJobExecutionRequest(
      Process->Driver->Context, Frame, Frame->Input, &Request);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  Downstream.Header = (NevercABITableHeader){
      sizeof(Downstream), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Status = Continuation->InvokeNext(Continuation, Frame, &Downstream);
  if (Status.Code != NEVERC_STATUS_OK)
    return Status;

  *OutResult = (NevercPhaseResult){0};
  OutResult->Header = (NevercABITableHeader){
      sizeof(*OutResult), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  OutResult->Action = NEVERC_PHASE_CONTINUE;
  return neverc_status_ok();
}
```

ويربط التسجيل كلًّا منهما بمرحلته:

```c
Observer.Phase = phase_id(NEVERC_PHASE_DRIVER_RAW_ARGUMENTS_HIGH,
                          NEVERC_PHASE_DRIVER_RAW_ARGUMENTS_LOW);
Observer.Points = NEVERC_OBSERVER_BEFORE | NEVERC_OBSERVER_AFTER;
Observer.Callback = observe_arguments;
Observer.UserData = Process;
Registrar->RegisterObserver(RegistrarContext, &Observer);

Interceptor.Phase = phase_id(NEVERC_PHASE_DRIVER_EXECUTE_JOB_HIGH,
                             NEVERC_PHASE_DRIVER_EXECUTE_JOB_LOW);
Interceptor.Callback = intercept_job;
Interceptor.UserData = Process;
Registrar->RegisterInterceptor(RegistrarContext, &Interceptor);
```

البناء والتشغيل:

```sh
cmake --build build-neverc --target neverc-plugin-example-driver-trace
build-neverc/bin/neverc \
  -fplugin=build-neverc/neverc/pluginsdk/examples/host/DriverTracePlugin.so \
  --driver-trace -c input.c -o input.o
```

## القواعد

- تتطلب تعديلات الوسائط والوسائط المُحلَّلة وسلسلة الأدوات ومخطط الإجراءات
  ومخطط المهام جميعُها `NevercPhaseContinuation` الخاصة بالمُعترِض؛ وخارجه
  تُرفَض بـ`NEVERC_STATUS_WRONG_SCOPE`.
- استدعِ `InvokeNext` مرة واحدة على الأكثر، وعلى خيط النداء الراجع فقط.
- يجب أن يصل كل مقبض تعديل إلى `Commit*` أو `Abort*` واحد بالضبط.
- العروض التي يعيدها نداء `Get*` مُستعارة طوال مدة النداء الراجع. انسخ ما تريد
  الاحتفاظ به.
- لا يجوز لنداء `NEVERC_JOB_PLUGIN` الراجع أن يُطلق العملية التي كان المضيف
  سيطلقها ثم يُبلِّغ أيضًا بنجاح المسار المدمج؛ أعلن `REPLACE` وتحمَّل النتيجة.
- أبلِغ عن فشل مهمة عبر `NevercJobResultDescriptor.ExecutionFailed` و
  `ErrorMessage` بدل إعادة حالة غير OK لمهمة نُفِّذت وفشلت فشلًا مشروعًا.

راجع `PluginDriver.h` للتصريحات المعيارية، و`PhaseSchema.json` لسياسات مراحل
المشغّل، و`coverage.json` لأدلة الاختبار.
