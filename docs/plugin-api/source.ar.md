<div dir="rtl">

**اللغات**: [English](source.md) | [简体中文](source.zh-CN.md) | [繁體中文](source.zh-TW.md) | [日本語](source.ja.md) | [한국어](source.ko.md) | [Français](source.fr.md) | [Deutsch](source.de.md) | [Español](source.es.md) | [Italiano](source.it.md) | [Русский](source.ru.md) | [العربية](source.ar.md)

[← واجهة NeverC الثنائية للإضافات](README.ar.md)

# واجهة المصادر والإدخال/الإخراج لإضافات NeverC

تنشر [`PluginSource.h`] جدولين. `NevercIOAPI` هي نظام الملفات: مزوّدو الملفات
الافتراضية، والقراءة، واجتياز الأدلة، ومصارف الإخراج، وتسجيل التبعيات. أما
`NevercSourceLocationAPI` فتعيد مواضع المترجم الداخلية إلى ملفات وأسطر ونص
مكتوب. وبهما معًا تستطيع الإضافة أن تقدّم ترويسة لا وجود لها إلا في الذاكرة، أو
أن تحلّ توسيع ماكرو حتى موضع كتابته، أو أن تكتب خرجًا جانبيًا يشارك في حساب
ديمومة البناء.

## الواجهات

```c
#include "neverc/Plugin/PluginSource.h"
```

| الواجهة | الجدول | وحدات ماكرو الإصدار |
|---|---|---|
| `NEVERC_INTERFACE_IO_{HIGH,LOW}` | `NevercIOAPI` | `NEVERC_IO_API_MAJOR` / `_MINOR` |
| `NEVERC_INTERFACE_SOURCE_LOCATION_{HIGH,LOW}` | `NevercSourceLocationAPI` | `NEVERC_SOURCE_LOCATION_API_MAJOR` / `_MINOR` |

`NEVERC_SOURCE_API_MAJOR` و`_MINOR` اسمان مرادفان لزوج source-location.

## مراحل المصدر الثلاث

| المرحلة | السياسة | المعنى |
|---|---|---|
| `neverc.source.resolve_input` | OBSERVABLE، INTERCEPTABLE | تحويل دخل المشغّل إلى دخل مصدري |
| `neverc.source.open` | إضافةً إلى REPLACEABLE | إنتاج وحدة المصدر لدخل ما |
| `neverc.source.after_open` | OBSERVABLE | إشعار بأن وحدةً صارت متاحة |

ولأن `neverc.source.open` قابلة للاستبدال، يستطيع المزوّد أن يعيد وحدةً ركّب
بايتاتها بنفسه، وهذه هي الطريقة المدعومة لحقن شيفرة مولَّدة دون لمس القرص.

## مزوّدو نظام الملفات الافتراضي

يستحوذ مزوّد VFS على بادئة مسار، ويجيب عن الأسئلة الأربعة التي يطرحها المترجم
عن الملف.

```c
typedef struct NevercVFSProviderDescriptor {
  NevercABITableHeader Header;
  NevercStringView ProviderID;
  NevercStringView RoutePrefix;
  NevercBool Deterministic;
  NevercBool Cacheable;
  uint64_t Reserved;
  NevercVFSPathPredicateFn MatchesPath;
  NevercVFSProviderStatusFn Status;
  NevercVFSProviderOpenReadFn OpenRead;
  NevercVFSProviderReadDirectoryFn ReadDirectory;
  NevercVFSProviderCanonicalizeFn Canonicalize;
  void *UserData;
  NevercDestroyUserDataFn DestroyUserData;
} NevercVFSProviderDescriptor;
```

يملأ كل ردّ نداء نتيجةً يبيّن حقل `Disposition` فيها ما إذا كان المزوّد قد عالج
الطلب:

```c
static NevercStatus NEVERC_CALL
open_read(NevercTaskHandle Task, NevercStringView Path, void *UserData,
          NevercVFSOpenReadResult *OutResult) {
  static const char Header[] = "#define GENERATED 1\n";
  if (!path_matches(Path)) {
    OutResult->Disposition = NEVERC_VFS_RESULT_NOT_HANDLED;
    return neverc_status_ok();
  }
  OutResult->Disposition   = NEVERC_VFS_RESULT_HANDLED;
  OutResult->Status.Type   = NEVERC_VFS_FILE_REGULAR;
  OutResult->Status.Size   = sizeof(Header) - 1;
  OutResult->Content.Data  = (const uint8_t *)Header;
  OutResult->Content.Length = sizeof(Header) - 1;
  OutResult->Content.NullTerminated = NEVERC_TRUE;
  return neverc_status_ok();
}
```

إعادة `NEVERC_VFS_RESULT_NOT_HANDLED` تمرّر الطلب إلى المزوّد التالي، ثم إلى
نظام الملفات الحقيقي في النهاية. وأنواع الملفات هي
`NEVERC_VFS_FILE_UNKNOWN` و`REGULAR` و`DIRECTORY` و`SYMLINK` و`OTHER`.

يتم التسجيل أثناء `Register`:

```c
IO->RegisterVFSProvider(IO->Context, RegistrarContext, &Descriptor);
```

أما إن أردت ملفًا واحدًا في الذاكرة يكفي أن يوجد طوال جلسة واحدة، فتجاوز
المزوّد كليًا:

```c
IO->AddMemoryFile(IO->Context, Session, SV("/virtual/config.h"),
                  Content, ModificationTime);
```

[`pluginsdk/examples/VirtualHeaderPlugin.c`] مزوّد كامل وعامل.

## قراءة الملفات

```c
NevercVFSStatus Status;
IO->Stat(IO->Context, Task, Path, &Status);

NevercFileHandle File;
IO->OpenFileForRead(IO->Context, Task, Path, &File);

NevercBufferHandle Buffer;
IO->ReadFile(IO->Context, Task, File, /*Offset=*/0, /*Length=*/Status.Size,
             &Buffer);

NevercBufferView View;
IO->GetBufferView(IO->Context, Task, Buffer, &View);
/* View.Data / View.Length / View.NullTerminated */

IO->ReleaseBuffer(IO->Context, Task, Buffer);
IO->CloseFile(IO->Context, Task, File);
```

تحوّل `CopyBuffer` بايتاتٍ تملكها أنت إلى مخزن مؤقت لدى المضيف، وتحلّ
`Canonicalize` المسار، وتتولى `GetWorkingDirectory` / `SetWorkingDirectory`
الدليل الحالي للمهمة. أما الأدلة فتُجتاز بـ`OpenDirectory` و`ReadDirectory`
(التي تضبط `OutHasEntry` إلى `NEVERC_FALSE` عند النهاية) و`CloseDirectory`.

تُبلَّغ رموز أخطاء الإدخال/الإخراج في `NevercStatus.Detail`:
`NEVERC_IO_ERROR_NOT_FOUND` و`PERMISSION_DENIED` و`NOT_DIRECTORY`
و`IS_DIRECTORY` و`INVALID_PATH` و`IO`.

## كتابة المخرجات

المخرجات معامَلاتية. تفتح مصرفًا، ثم تكتب، ثم تُنهي لتحصل على ختم — حجمٍ وبصمةٍ
من 32 بايت يستطيع نظام البناء التحقق منها.

```c
NevercOutputSinkHandle Sink;
IO->BeginFileOutput(IO->Context, Task, SV("out.json"), /*SizeBudget=*/0, &Sink);
IO->OutputWrite(IO->Context, Task, Sink, Bytes);
IO->OutputMetadataSet(IO->Context, Task, Sink, SV("content-type"),
                      SV("application/json"));

NevercOutputSeal Seal = {0};
Seal.Header = (NevercABITableHeader){sizeof(Seal), NEVERC_IO_API_MAJOR,
                                     NEVERC_IO_API_MINOR, 0};
IO->OutputFinish(IO->Context, Task, Sink, &Seal);
```

| الدالة | الغرض |
|---|---|
| `BeginMemoryOutput` | مصرف مسنود بالذاكرة ومسمّى منطقيًا |
| `BeginFileOutput` | مصرف يحطّ ذريًّا عند مسار نهائي |
| `BeginStreamOutput` | مصرف على `NEVERC_OUTPUT_STREAM_STDOUT` أو `_STDERR` |
| `OutputWrite`، `OutputWriteAt` | الإلحاق، أو الكتابة عند إزاحة |
| `OutputTell`، `OutputTruncate` | التحكم بالموضع والحجم |
| `OutputMetadataSet` | إرفاق زوج مفتاح/قيمة بالخرج |
| `OutputFinish` | ختم الخرج وإنتاج `NevercOutputSeal` |
| `OutputAbort` | إسقاط كل ما كُتب |
| `OutputGetSummary` | فحص الحالة والرايات والحجم والبصمة في أي وقت |

تنتقل `NevercOutputSummary.State` عبر `NEVERC_OUTPUT_OPEN` و`FINISHED`
و`COMMITTED` و`ABORTED` و`FAILED_PARTIAL`، بينما تسجّل `Flags` كلًّا من
`PUBLISHED` و`DURABLE` و`MAY_BE_PARTIAL` و`RECOVERY_REQUIRED`
و`DURABILITY_UNCONFIRMED`. وهذه الرايات هي نفس المعلومات التي يظهرها المشغّل في
`NevercStatus.Flags`، فيمكن تمييز انهيارٍ في منتصف الكتابة عن إخفاق نظيف.

قيمة `SizeBudget` صفرًا تعني بلا حدّ؛ أما الميزانية غير الصفرية فتجعل التجاوز
يفشل بـ`NEVERC_STATUS_RESOURCE_EXHAUSTED` بدلًا من ملء القرص.

## تسجيل التبعيات

إن قرأت الإضافة شيئًا ينبغي لنظام البناء تتبّعه، فصرّح بذلك. وإلا فلن تعيد
البناءات التزايدية البناء عندما يتغير ذلك الدخل.

```c
NevercDependencyDescriptor Dependency = {0};
Dependency.Header = (NevercABITableHeader){sizeof(Dependency),
                                           NEVERC_IO_API_MAJOR,
                                           NEVERC_IO_API_MINOR, 0};
Dependency.CanonicalPath = SV("/etc/mytool/rules.txt");
Dependency.ContentDigest = Digest;
Dependency.Kind          = NEVERC_INPUT_DEPENDENCY_RESOURCE;
Dependency.System        = NEVERC_FALSE;
Dependency.ProviderID    = SV("com.example.myplugin");

NevercDependencyHandle Handle;
IO->RecordDependency(IO->Context, Task, &Dependency, &Handle);
```

والأنواع هي `NEVERC_INPUT_DEPENDENCY_SOURCE` و`INCLUDE` و`MODULE` و`RESOURCE`
و`TOOL` و`PLUGIN`.

## مواضع المصدر

`NevercSourceLocation` معتِمة. وجدول المواضع يحوّلها إلى شيء يمكنك طباعته أو
مقارنته.

```c
NevercSourceLocationInfo Info = {0};
Info.Header = (NevercABITableHeader){sizeof(Info),
                                     NEVERC_SOURCE_LOCATION_API_MAJOR,
                                     NEVERC_SOURCE_LOCATION_API_MINOR, 0};
Source->GetLocationInfo(Source->Context, Task, Location, &Info);
/* Info.Kind هي NEVERC_SOURCE_LOCATION_FILE أو _MACRO؛
   ثم تأتي Info.FileOffset وInfo.Line وInfo.Column. */
```

هناك أربعة تحويلات تنقّلك بين رؤى الموضع الواحد، وكلها تتشارك التوقيع
`NevercTransformSourceLocationFn`:

| الدالة | ما تُعيده |
|---|---|
| `GetSpellingLocation` | حيث كُتبت محارف الرمز فعليًا |
| `GetExpansionLocation` | حيث يظهر توسيع الماكرو في المصدر |
| `GetFileLocation` | أقرب موضع ملف |
| `GetIncludeLocation` | تعليمة `#include` التي جلبت الملف |
| `GetTokenEnd` | الموضع التالي لآخر محرف في الرمز |

تطبّق `GetPresumedLocation` توجيهات `#line` وتعطي اسم ملف وسطرًا وعمودًا وموضع
تضمين. ويمنحك `GetLocationFile` مع `GetFileInfo` المسار المعياري والحجم ووقت
التعديل والمعرّف الفريد، وما إذا كان الملف للمستخدم أو للنظام أو لنظام extern-C:

```c
typedef struct NevercFileInfo {
  NevercABITableHeader Header;
  NevercStringView Path;
  NevercStringView CanonicalPath;
  uint64_t Size;
  int64_t ModificationTime;
  NevercFileUniqueID UniqueID;      /* {Device, File} */
  NevercFileCharacteristic Characteristic;
  NevercBool NamedPipe;
} NevercFileInfo;
```

وتُقرأ المدَيات بـ`GetRangeInfo` (التي تبلّغ عن `Begin` و`End` وعمّا إذا كان
المدى `NEVERC_SOURCE_RANGE_CHARACTER` أم `_TOKEN`)، أما البايتات نفسها
فبـ`GetSourceText` أو `GetCharacterData`.

وحين تحتاج مواضع كثيرة دفعةً واحدة — كتمريرة تشخيص على دالة كاملة مثلًا —
استعمل الصيغة الدُفعية بدل نداء لكل موضع:

```c
Source->GetLocationInfoBatch(Source->Context, Task, Locations, LocationCount,
                             OutInfos, OutInfoCapacity);
```

## وحدات المصدر

رؤية الدخل وبايتاته على مستوى المراحل:

```c
NevercSourceInputInfo Input = {0};
Source->GetSourceInput(Source->Context, Frame, Frame->Input, &Input);
/* Input.Path و.Kind (FILE أو BUFFER) و.Language و.System و.Preprocessed */
```

ويجيب مزوّد `neverc.source.open` بوحدة مسنودة بالذاكرة:

```c
NevercMemorySourceUnitDescriptor Unit = {0};
Unit.Header = (NevercABITableHeader){sizeof(Unit),
                                     NEVERC_SOURCE_LOCATION_API_MAJOR,
                                     NEVERC_SOURCE_LOCATION_API_MINOR, 0};
Unit.LogicalPath      = SV("/virtual/generated.c");
Unit.CanonicalIdentity = SV("com.example:generated:v1");
Unit.Content          = Bytes;
Unit.ProviderID       = SV("com.example.myplugin");
Unit.Deterministic    = NEVERC_TRUE;
Unit.Cacheable        = NEVERC_TRUE;

NevercArtifactHandle Output;
Source->CreateMemorySourceUnit(Source->Context, Frame, Frame->Input, &Unit,
                               &Output);
```

`CanonicalIdentity` هي ما تفهرس عليه الذاكرة المخبّأة، فوجب أن تتغير كلما تغيّر
المحتوى. وتقرأ `GetSourceUnit` الوحدة مجددًا، وتبلّغ زيادةً عن `MemoryBacked`.

## القواعد

- المخازن المؤقتة الآتية من `ReadFile` و`CopyBuffer` و`PathToBuffer` مملوكة
  للمضيف؛ حرّر كلًّا منها بـ`ReleaseBuffer`.
- كل `OpenFileForRead` يحتاج `CloseFile`، وكل `OpenDirectory` يحتاج
  `CloseDirectory`، وكل مصرف إخراج يحتاج `OutputFinish` أو `OutputAbort`.
- الرؤى داخل `NevercFileInfo` و`NevercVFSStatus` ونتائج المواضع مستعارة طوال
  ردّ النداء فقط.
- يعمل ردّ نداء مزوّد VFS على خيط المهمة، ويجب ألا يعاود استدعاء المترجم؛ أجب
  من بيانات تملكها سلفًا.
- صرّح بـ`Deterministic` و`Cacheable` بصدق. فالمزوّد الذي يقرأ الساعة أو البيئة
  ثم يدّعي الحتمية سينتج ذاكرة بناء مخبّأة مسمومة.
- نطاق `AddMemoryFile` هو الجلسة؛ وحين يعتمد المحتوى على المهمة يكون المزوّد هو
  الأداة الصحيحة.

انظر [`PluginSource.h`] للتصريحات المعيارية، و
[`pluginsdk/examples/VirtualHeaderPlugin.c`] لمزوّد كامل.

<!-- reference links -->
[`pluginsdk/examples/VirtualHeaderPlugin.c`]: ../../pluginsdk/examples/VirtualHeaderPlugin.c
[`PluginSource.h`]: ../../neverc/include/neverc/Plugin/PluginSource.h

</div>
