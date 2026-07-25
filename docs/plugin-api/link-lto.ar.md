**اللغات**: [English](link-lto.md) | [简体中文](link-lto.zh-CN.md) | [繁體中文](link-lto.zh-TW.md) | [日本語](link-lto.ja.md) | [한국어](link-lto.ko.md) | [Français](link-lto.fr.md) | [Deutsch](link-lto.de.md) | [Español](link-lto.es.md) | [Italiano](link-lto.it.md) | [Русский](link-lto.ru.md) | [العربية](link-lto.ar.md)

# واجهة الربط وLTO لإضافات NeverC

يُنمذَج الربط بوصفه **آلة حالات فوق مخطط واحد**. تكشف `PluginLink.h` هذا
المخطط — المدخلات، والمقاطع، والذرّات، والرموز، والحواف، وCOMDAT، والاستيرادات،
والتصديرات، وسجلات فك الكدس، والعناصر المُصطنَعة، وقيود التخطيط — إضافةً إلى
المراحل العشرين التي تنقله من قائمة ملفات إلى صورة ثنائية مُثبَّتة. أما
`PluginLTO.h` فتغطي المرحلتين الوسطيتين حيث يتحول bitcode إلى كائنات.

تستطيع الإضافة مراقبة كل خطوة، واعتراض معظمها، واستبدال خطوة واحدة، أو استبدال
الربط بأكمله، أو دمج الكائنات. ولا ترى أبدًا بنية بيانات من lld: فالمخطط إسقاط
مُطبَّع تنعكس عليه خلفيات ELF وCOFF وMach-O جميعًا.

## الواجهات

```c
#include "neverc/Plugin/PluginLink.h"
#include "neverc/Plugin/PluginLTO.h"   /* يتضمّن PluginLink.h */
```

| الواجهة | الجدول | الغرض |
|---|---|---|
| `NEVERC_INTERFACE_LINK_{HIGH,LOW}` | `NevercLinkAPI` | قراءة مخطط الربط وتعديله (52 فتحة) |
| `NEVERC_INTERFACE_LINK_REGISTRAR_{HIGH,LOW}` | `NevercLinkRegistrarAPI` | تسجيل مزوِّدي الرابط ودمج الكائنات والتحقق من الصورة |
| `NEVERC_INTERFACE_LINK_PHASE_{HIGH,LOW}` | `NevercLinkPhaseAPI` | الوصول إلى المخطط أو الصورة خلف `NevercArtifactHandle` |
| `NEVERC_INTERFACE_LTO_{HIGH,LOW}` | `NevercLTOAPI` | قراءة طلب LTO والوحدات وتحليلات الرموز |
| `NEVERC_INTERFACE_LTO_REGISTRAR_{HIGH,LOW}` | `NevercLTORegistrarAPI` | تسجيل مزوِّد توليد شيفرة LTO |

الخمس جميعها `NEVERC_INTERFACE_STABLE` عند الإصدار الرئيسي 1، فلا يملك المضيف
الأحدث إلا الإلحاق. اقرن كلًّا منها بـ`NEVERC_LINK_API_MAJOR` /
`NEVERC_LTO_API_MAJOR` المناسب، وتحقَّق من `TableSize` مقابل آخر فتحة تستدعيها.

## آلة الحالات

`NevercLinkGraphInfo.State` واحدة من أربع عشرة قيمة، وثلاث عشرة من المراحل
العشرين موجودة لغرض واحد: دفعها خطوة إلى الأمام:

| المرحلة | `NEVERC_LINK_STATE_…` الناتجة | مُتحقِّق المضيف |
|---|---|---|
| — | `INITIAL` | — |
| `neverc.link.input_probe` | `INPUT_PROBED` | `verify_input_probe` |
| `neverc.link.read_inputs` | `INPUTS_READ` | `verify_inputs` |
| `neverc.link.lto_resolve` | `LTO_RESOLUTION_READY` | |
| `neverc.link.lto_generate` | `LTO_GENERATED` | |
| `neverc.link.resolve_symbols` | `SYMBOLS_RESOLVED` | |
| `neverc.link.select_comdat` | `COMDAT_SELECTED` | |
| `neverc.link.gc` | `GC_COMPLETE` | `verify_liveness` |
| `neverc.link.icf` | `ICF_COMPLETE` | |
| `neverc.link.synthesize` | `SYNTHETICS_READY` | |
| `neverc.link.relax_thunks` | `THUNKS_RELAXED` | `verify_relaxation` |
| `neverc.link.layout` | `LAYOUT_COMPLETE` | `verify_layout` |
| `neverc.link.relocate` | `RELOCATIONS_APPLIED` | |
| `neverc.link.emit_image` | `IMAGE_EMITTED` | |

كل واحدة من هذه الثلاث عشرة هي
`OBSERVABLE | INTERCEPTABLE | REPLACEABLE | SKIPPABLE_WITH_PROOF`، فيجوز
لمزوِّد أن يقدّم الانتقال بنفسه، ويجوز لإضافة تحمل `NevercLinkProofHandle`
صالحًا أن تتخطّاه.

أما السبع الباقية فبنيوية:

| المرحلة | السياسة | الدور |
|---|---|---|
| `neverc.link.full` | OBSERVABLE، INTERCEPTABLE، REPLACEABLE | استبدال الربط كله، من `INITIAL` مباشرةً إلى صورة ثنائية |
| `neverc.link.object_merge` | OBSERVABLE، INTERCEPTABLE، REPLACEABLE | دمج `-r` القابل لإعادة التموضع لمخططات ObjectGraph |
| `neverc.link.post_emit` | OBSERVABLE، INTERCEPTABLE | آخر فرصة لملامسة بايتات الصورة |
| `neverc.link.image_verify` | OBSERVABLE، **مختومة** | مُتحقِّق صورة المضيف |
| `neverc.link.side_outputs_verify` | OBSERVABLE، **مختومة** | ملفات الخرائط وdSYM والآثار الجانبية |
| `neverc.link.commit` | OBSERVABLE، **مختومة** | النشر الذرّي لحزمة الخرج |
| `neverc.link.after_commit` | OBSERVABLE | إشعار ما بعد التثبيت |

البوابات المختومة الثلاث يمكن مراقبتها، لكن لا يمكن اعتراضها أو استبدالها أو
تخطّيها أبدًا. وقيمة `NEVERC_BUILTIN_LINK_PHASE_COUNT` هي 20.

## الوصول إلى المخطط من مرحلة

تحوِّل `NevercLinkPhaseAPI` أثرَ الإطار إلى مقبض قابل للاستخدام:

```c
NevercLinkPhaseGraphInfo GraphInfo = {0};
GraphInfo.Header = (NevercABITableHeader){sizeof(GraphInfo),
                                          NEVERC_LINK_PHASE_API_MAJOR,
                                          NEVERC_LINK_PHASE_API_MINOR, 0};
LinkPhase->GetGraph(LinkPhase->Context, Frame, Frame->Input, &GraphInfo);
/* GraphInfo.Link و.Graph و.Proof و.State و.Generation */
```

`GraphInfo.Link` هو `NevercLinkAPI` المرتبط بهذه المَهمة، فلا يحتاج المراقب إلى
`QueryInterface` منفصل. وينشر المزوِّد نتيجته بـ`PublishGraph`، بينما تفعل
`GetImage` الشيء نفسه لأثر صورة، فتُرجع `NevercLinkPhaseImageInfo` يحمل الصورة
وحزمة الخرج و`NevercBinaryImageState` (`CANDIDATE` أو `VERIFIED` أو
`COMMITTED` أو `ABORTED` أو `FAILED_PARTIAL`).

## قراءة المخطط

`NevercLinkGraphInfo` هو الملخّص — الهدف، والصيغة، والحالة، والجيل، وسبعة عشر
عدّادًا للكيانات، و`SemanticDigest` بطول 32 بايت. أما الكيانات نفسها فتعود عبر
نداء ترقيم صفحات واحد لكل نوع، وكلها تتشارك صفحة يملكها المُستدعي:

```c
typedef struct NevercLinkEntityPage {
  NevercABITableHeader Header;
  void *Data;                /* مصفوفة تُوفّرها وتملكها          */
  uint64_t ElementCapacity;  /* كم مدخلًا تتّسع                  */
  uint64_t ElementStride;    /* sizeof عنصرك                     */
  uint64_t OutCount;         /* كم كتب المضيف                    */
  uint64_t NextCursor;       /* أعده للمتابعة                    */
  NevercBool HasMore;
  uint32_t Reserved;
} NevercLinkEntityPage;
```

لا يكتب المضيف أكثر من `ElementCapacity` مدخلًا بحجم `ElementStride` بايت لكل
منها، ولا يحتفظ بـ`Data` أبدًا، فتكفي مصفوفة على المكدس:

```c
NevercLinkSymbolInfo Symbols[64];
NevercLinkEntityPage Page = {0};
uint64_t Cursor = 0;

do {
  Page.Header = (NevercABITableHeader){sizeof(Page), NEVERC_LINK_API_MAJOR,
                                       NEVERC_LINK_API_MINOR, 0};
  Page.Data            = Symbols;
  Page.ElementCapacity = 64;
  Page.ElementStride   = sizeof(Symbols[0]);
  Status = Link->GetSymbolPage(Link->Context, Task, Graph, Cursor, &Page);
  if (Status.Code != NEVERC_STATUS_OK)
    break;
  for (uint64_t I = 0; I != Page.OutCount; ++I) {
    /* Symbols[I].Name و.Binding و.Definition و.IsPrevailing و… */
  }
  Cursor = Page.NextCursor;
} while (Page.HasMore);
```

خمسة عشر مُرقِّم صفحات للمخطط تتبع هذا الشكل — `GetInputPage` و
`GetArchivePage` و`GetArchiveMemberPage` و`GetSharedLibraryPage` و
`GetBitcodeModulePage` و`GetSectionPage` و`GetAtomPage` و`GetSymbolPage` و
`GetEdgePage` و`GetComdatPage` و`GetImportPage` و`GetExportPage` و
`GetUnwindPage` و`GetSyntheticPage` و`GetConstraintPage` — واثنان آخران هما
`GetBinarySegmentPage` و`GetBinarySectionPage` يُرقِّمان صفحات صورة مُنتَجة.
ولكلٍّ منها `Get…Info` مقابلة لمقبض مفرد.

كل معلومات كيان تحمل `NevercLinkOrigin`:

```c
typedef struct NevercLinkOrigin {
  NevercABITableHeader Header;
  NevercLinkInputHandle Input;
  NevercLinkArchiveMemberHandle ArchiveMember;
  NevercObjectGraphHandle ObjectGraph;
  uint64_t ObjectEntityID;
  NevercInterfaceID CreatedByPhase;
  NevercStringView CreatedByProvider;
  NevercInterfaceID LastMutationPhase;
  NevercStringView LastMutationPlugin;
} NevercLinkOrigin;
```

هذا ما يجعل الربط قابلًا للتدقيق: لأي ذرّة في الخرج تستطيع تسمية ملف الدخل،
وعضو الأرشيف الذي سُحبت منه، والمرحلة التي أنشأتها، والإضافة التي لمستها أخيرًا.

### الكيانات

| النوع | بنية Info | حقول بارزة |
|---|---|---|
| Input | `NevercLinkInputInfo` | `Kind` (OBJECT، ARCHIVE، SHARED_LIBRARY، BITCODE، SCRIPT، BLOB)، `Ordinal`، `ContentDigest`، `ReaderRoute` |
| أرشيف / عضو | `NevercLinkArchiveInfo`، `NevercLinkArchiveMemberInfo` | `Thin`، `Materialized`، `MaterializationReason` |
| مكتبة مشتركة | `NevercLinkSharedLibraryInfo` | `InstallName` |
| وحدة bitcode | `NevercLinkBitcodeModuleInfo` | `Summary` |
| مقطع | `NevercLinkSectionInfo` | `Kind`، `Flags`، `Alignment`، `Address`، `Size`، `Comdat` |
| ذرّة | `NevercLinkAtomInfo` | `Flags`، `Content`، `ZeroFillSize`، `FoldLeader` |
| رمز | `NevercLinkSymbolInfo` | `Binding`، `Visibility`، `Definition`، `IsPrevailing`، `IsRoot` |
| حافة | `NevercLinkEdgeInfo` | `Kind`، `Offset`، `RelocationKind`، `Addend`، `TargetSymbol`، `TargetAtom` |
| COMDAT | `NevercLinkComdatInfo` | `Selection`، `Selected` |
| استيراد / تصدير | `NevercLinkImportInfo`، `NevercLinkExportInfo` | `Library`، `Symbol` |
| فك كدس | `NevercLinkUnwindInfo` | `PersonalitySymbol` |
| مُصطنَع | `NevercLinkSyntheticInfo` | `Role`، `Section`، `Atom` |
| قيد | `NevercLinkConstraintInfo` | `Kind`، `SubjectID`، `Value`، `Required` |

رايات الذرّة هي `LIVE` و`ROOT` و`SYNTHETIC` و`FOLDED` و
`ADDRESS_SIGNIFICANT` و`TLS` و`UNWIND`. وارتباطات الرموز هي `LOCAL` و`GLOBAL`
و`WEAK` و`COMMON`؛ والتعريفات `UNDEFINED` و`DEFINED` و`ABSOLUTE` و`COMMON`
و`SHARED`. وأنواع الحواف `RELOCATION` و`ASSOCIATION` و`KEEP_ALIVE` و`UNWIND`
و`FORMAT_EXTENSION`. ويشمل اختيار COMDAT القيم `ANY` و`EXACT_MATCH` و
`SAME_SIZE` و`LARGEST` و`NEWEST` و`NO_DUPLICATES`.

## تعديل المخطط

التعديل معاملاتي، ونطاقه دائمًا مخطط واحد:

```c
NevercLinkMutationHandle Mutation;
Link->BeginMutation(Link->Context, Task, Graph, &Mutation);

Link->SetSymbolRoot(Link->Context, Task, Mutation, Symbol, NEVERC_TRUE);
Link->ReplaceAtomContent(Link->Context, Task, Mutation, Atom,
                         (NevercByteView){Bytes, Length},
                         /*ZeroFillSize=*/0);

Status = Link->CommitMutation(Link->Context, Task, Mutation);
if (Status.Code != NEVERC_STATUS_OK)
  Link->AbandonMutation(Link->Context, Task, Mutation);
```

يُجهِّز التثبيت نسخة عمل، ويتحقق منها، وعندئذ فقط ينشر ويزيد `Generation`.
وتتخلص `AbandonMutation` من كل شيء. فالتثبيت والمخطط عند `GC_COMPLETE` مثلًا
يُعيد تشغيل مُتحقِّق الحيوية، فيُرفَض أي تعديل يترك ذرّة حيّة معزولة بدل أن
يُكتَب.

### التعديلات تُبطِل الحالات اللاحقة

هذا هو الجزء الذي يفاجئ الناس. كل نداء تجهيز يُصنَّف، والتصنيف يحدّد **أبكر
حالة تصبح باطلة**؛ وعلى المضيف إعادة تشغيل كل مرحلة ابتداءً من هناك:

| النداء | أبكر حالة تُبطَل |
|---|---|
| `RebindSymbol`، `RetargetEdge` | `SYMBOLS_RESOLVED` |
| `SetSymbolResolution` | `COMDAT_SELECTED` |
| `SetSymbolRoot` | `GC_COMPLETE` |
| `SetAtomLive` | `ICF_COMPLETE` |
| `SetFoldLeader`، `ReplaceAtomContent` | `SYNTHETICS_READY` |
| `CreateSynthetic`، `ReplaceSynthetic`، `EraseSynthetic` | `SYNTHETICS_READY` |
| `CreateConstraint`، `ReplaceConstraint`، `EraseConstraint` | `LAYOUT_COMPLETE` |

والتعديل الذي يمسّ عدة بنود يأخذ أبكرها. لذا فإعادة ربط رمز بعد التخطيط تُلقي
بنتائج التخطيط وإعادة التموضع والصورة — وهو رخيص أثناء `gc` وباهظ أثناء
`post_emit`. عدِّل في أبكر موضع تسمح به تغييرتك في آلة الحالات.

تأخذ `SetSymbolResolution` سجل تحديث صغيرًا بدل رمز كامل، فيمنع ذلك تغيير
التحليل من إعادة كتابة اسم أو قيمة عن غير قصد:

```c
NevercLinkSymbolResolutionUpdate Update = {0};
Update.Header = (NevercABITableHeader){sizeof(Update), NEVERC_LINK_API_MAJOR,
                                       NEVERC_LINK_API_MINOR, 0};
Update.Binding      = NEVERC_LINK_SYMBOL_BINDING_GLOBAL;
Update.Visibility   = NEVERC_LINK_SYMBOL_VISIBILITY_HIDDEN;
Update.Definition   = NEVERC_LINK_SYMBOL_DEFINED;
Update.IsPrevailing = NEVERC_TRUE;
Update.IsExported   = NEVERC_FALSE;
Link->SetSymbolResolution(Link->Context, Task, Mutation, Symbol, &Update);
```

## تخطّي مرحلة ببرهان

تقبل المرحلة `SKIPPABLE_WITH_PROOF` مقبضَ `NevercLinkProofHandle` بدل أن
تُنفَّذ. ويثبّت البرهان كل ما يعتمد عليه التخطّي:

```c
typedef struct NevercLinkProofInfo {
  NevercABITableHeader Header;
  NevercLinkProofHandle Proof;
  NevercLinkGraphHandle Graph;
  NevercLinkState State;
  uint32_t Reserved;
  uint64_t GraphGeneration;
  NevercTargetID TargetID;
  NevercObjectFormatID FormatID;
  NevercInterfaceID OutputArtifact;
  uint8_t RouteDigest[32];
  uint8_t SemanticDigest[32];
  uint64_t ImageBase;
  uint64_t EntryAddress;
} NevercLinkProofInfo;
```

ولأن كلًّا من `GraphGeneration` و`SemanticDigest` مُسجَّل، فإن أي تعديل مُثبَّت
بين إصدار البرهان واستعماله يجعله قديمًا، فيُشغّل المضيف المرحلة فعليًّا.

## الصورة الثنائية

بعد `emit_image` يصبح المنتَج `NevercBinaryImageHandle`:

```c
NevercBinaryImageInfo Image = {0};
Image.Header = /* … */;
Link->GetBinaryImageInfo(Link->Context, Task, ImageHandle, &Image);
/* .State و.OutputKind و.EntryAddress و.ImageBase و.Size و
   .SegmentCount و.SectionCount و.ImportCount و.ExportCount و
   .DynamicRelocationCount و.ContentDigest                     */
```

أنواع الخرج هي `RELOCATABLE` و`EXECUTABLE` و`SHARED_LIBRARY` و`BUNDLE`. ورايات
المقاطع هي `READ` و`WRITE` و`EXECUTE`.

`Image.Binary` و`Image.Builder` هما الكاتب المعاملاتي المحدود من
`PluginObject.h` — `Reserve` و`Write` و`WriteAt` و`Tell` و`ReadAt` و`Insert`
و`Append` و`Resize`. وعلى مُعترِض `post_emit` الذي يُرقِّع بايتات أن يمرّ عبره؛
والكتابة خارج الحد المحجوز تُجهض التجهيز بدل أن تُكبِّر الملف.

## المزوِّدون

سجِّل أثناء `Register` فقط، ولا تسجِّل بعده أبدًا.

### استبدال الرابط

```c
NevercLinkerProviderDescriptor Provider = {0};
Provider.Header = (NevercABITableHeader){sizeof(Provider),
                                         NEVERC_LINK_REGISTRAR_API_MAJOR,
                                         NEVERC_LINK_REGISTRAR_API_MINOR, 0};
Provider.ProviderID   = SV("com.example.my-linker");
Provider.TargetID     = MyTargetID;
Provider.InputFormat  = ELFFormatID;
Provider.OutputFormat = ELFFormatID;
Provider.OutputKind   = NEVERC_LINK_OUTPUT_EXECUTABLE;
Provider.Flags        = NEVERC_LINK_PROVIDER_DETERMINISTIC |
                        NEVERC_LINK_PROVIDER_CACHEABLE;
Provider.Link         = my_link;
Provider.VerifyImage  = my_verify;      /* اختياري */
LinkRegistrar->RegisterLinkerProvider(LinkRegistrar->Context,
                                      RegistrarContext, &Provider);
```

يتلقّى النداء الراجع الطلبَ ومجموعةَ المدخلات الخام، ويملأ مرشَّحًا:

```c
static NevercStatus NEVERC_CALL
my_link(void *UserData, NevercTaskHandle Task,
        const NevercLinkRequest *Request,
        const NevercRawLinkInputSet *Inputs,
        NevercLinkerProductCandidate *OutCandidate) {
  /* Request->Target و->OutputKind و->OutputURI و->Options و->RequestDigest
     Inputs->Inputs هي NevercRawLinkInput[]، و Inputs->OrderDigest يثبّت الترتيب */
  OutCandidate->Image     = MyImage;
  OutCandidate->Outputs   = MyBundle;
  OutCandidate->ProductID = MyProductID;
  return neverc_status_ok();
}
```

يحمل `NevercLinkOptions` الرايات التي يتفرّع عليها الرابط فعلًا — `PIE` و
`STATIC` و`GC_SECTIONS` و`ICF` و`EXPORT_DYNAMIC` و`ALLOW_UNDEFINED` و
`WHOLE_ARCHIVE` و`DETERMINISTIC` — إضافةً إلى `EntrySymbol` و`InstallName` و
`Soname` و`ImageBase` و`PageSize` و`ThreadBudget` ومسارات البحث والمكتبات. أما
رايات كل مدخل فهي `WHOLE_ARCHIVE` و`AS_NEEDED` و`START_GROUP` و`END_GROUP` و
`LAZY`.

عند النجاح يتبنّى المضيف المرشَّح. وعند الفشل يبقى ما أنشأه المزوِّد ملكًا له.
وتعمل بوابتا التحقق والتثبيت المختومتان في الحالتين.

### دمج الكائنات والتحقق من الصور

تتولّى `RegisterObjectMergeProvider` الخيار `-r`: يحمل الطلب
`NevercObjectMergeInput[]` الداخلة ومخطط خرج ومعاملة مفتوحين مسبقًا، فيكتب
المزوِّد داخل معاملة يملكها المضيف بدل بناء ملف.

وتضيف `RegisterBinaryImageVerifier` فحصًا للقراءة فقط يعمل إلى جانب مُتحقِّق
الصورة الخاص بالمضيف. ولا يمكنه أن يحلّ محلّه.

## LTO

تنتج `lto_resolve` تحليلات الرموز، وتحوِّل `lto_generate` الـ bitcode إلى
كائنات. و`NevercLTOAPI` تقرأ الاثنين.

```c
NevercLTORequest Request = {0};
Request.Header = /* … */;
LTO->GetRequest(LTO->Context, Task, RequestHandle, &Request);
/* .LinkRequest و.LinkGraph و.Target و.OutputFormat و.Options و
   .Modules و.Resolutions و.ResolutionDigest و.RequestDigest */
```

تستعمل `GetModulePage` و`GetResolutionPage` بروتوكول `NevercLinkEntityPage`
نفسه، فتملآن `NevercLTOInputModuleInfo` و`NevercLTOSymbolResolution`. ويُسمّي
كل تحليل الوحدةَ والرمزَ ومقبضَ `NevercLinkSymbolHandle` المقابل ورايتِه:

| الراية | المعنى |
|---|---|
| `PREVAILING` | هذه الوحدة تملك التعريف. |
| `VISIBLE_TO_REGULAR_OBJECT` | يستطيع كائن غير bitcode رؤيته. |
| `EXPORTED` | موجود في جدول الرموز الديناميكية. |
| `FINAL_DEFINITION` | لا يمكن لأي تعريف لاحق أن يحلّ محلّه. |
| `CAN_INLINE` | التضمين عبر الحدّ مسموح. |
| `CAN_INTERNALIZE` | التحويل إلى داخلي مسموح. |
| `LINKER_REDEFINED` | الرابط أعاد تعريفه. |
| `REFERENCED_BY_REGULAR_OBJECT` | يشير إليه كائن عادي. |

يختار `NevercLTOOptions` بين `NEVERC_LTO_FULL` و`NEVERC_LTO_THIN`، ومستويات
التحسين، و`ThreadBudget`، و`ThinBackendPartitions`، والمعالج والخصائص، ونطاق
ذاكرة مخبأة من `DISABLED` أو `TASK` أو `LOCAL_SHARED` أو `REMOTE_SHARED`.
ورايات الخيارات هي `EMIT_OPTIMIZED_BITCODE` و`EMIT_INDEX` و`SAVE_TEMPS` و
`WHOLE_PROGRAM_VISIBILITY` و`UNIFIED_LTO` و`DETERMINISTIC`.

### مزوِّد LTO

```c
NevercLTOProviderDescriptor Provider = {0};
Provider.Header = /* … */;
Provider.ProviderID    = SV("com.example.my-lto");
Provider.TargetID      = MyTargetID;
Provider.Flags         = NEVERC_LTO_PROVIDER_THIN |
                         NEVERC_LTO_PROVIDER_DETERMINISTIC |
                         NEVERC_LTO_PROVIDER_CACHEABLE;
Provider.BuildCacheKey = my_cache_key;
Provider.Codegen       = my_codegen;
LTORegistrar->RegisterProvider(LTORegistrar->Context, RegistrarContext,
                               &Provider);
```

تكتب `BuildCacheKey` في `NevercMutableByteView` يوفّره المُستدعي، وتُبلِّغ عن
الحجم الذي احتاجته، فيستطيع المضيف ضبط حجم المخزن وإعادة المحاولة. ويجب أن تكون
دالة صرفة للطلب — واشتقاقها من `RequestDigest` و`ResolutionDigest` هو البناء
الآمن. وإعلان `CACHEABLE` بمفتاح يتجاهل جزءًا من الطلب يُنتج كائنات قديمة تنجو
حتى من إعادة بناء نظيفة.

وتملأ `Codegen` بنية `NevercLTOProductCandidate`: مصفوفة من
`NevercLTOObjectProduct` (كل عنصر يسمّي وحدته المصدرية وObjectGraph والأثر)،
واختياريًّا `OptimizedBitcode` و`ThinIndex`، إضافةً إلى `CacheKey` المستعمل
فعلًا.

## القواعد

- المقابض نطاقها المَهمة ويملكها المضيف. لا تخزّن مقبضًا بعد النداء الراجع، ولا
  تستعمله في مَهمة أخرى، ولا تصطنع قيمة أبدًا.
- `NevercLinkEntityPage.Data` ملكك. يكتب المضيف على الأكثر
  `ElementCapacity × ElementStride` بايت ولا يحتفظ بأي إشارة إليه.
- كل `BeginMutation` يصل إلى `CommitMutation` أو `AbandonMutation` واحد
  بالضبط، بما في ذلك على مسار الخطأ.
- عدِّل في أبكر موضع تسمح به التغييرة داخل آلة الحالات؛ فالتعديل المتأخر يُبطل
  بصمت كل مرحلة لاحقة.
- لا تعدّل من مراقب. يحصل المراقبون على جسر للقراءة فقط، وتُرفَض المحاولة بـ
  `NEVERC_STATUS_POLICY_VIOLATION`.
- اكتب بايتات الصورة عبر `NevercBinaryImageInfo.Binary` وبانيها حصرًا. والفيض
  يُجهض التجهيز بدل أن يُكبِّر الخرج.
- لا تدّعِ `DETERMINISTIC` إلا إذا كانت بصمة الطلب نفسها تُنتج دائمًا خرجًا
  مطابقًا بايتًا ببايت، ولا تدّعِ `CACHEABLE` إلا إذا كان مفتاح ذاكرتك المخبأة
  يغطي كل مدخل قادر على تغيير ذلك الخرج.
- `image_verify` و`side_outputs_verify` و`commit` مختومة. راقبها، ولا تحاول
  اعتراضها أو تخطّيها.

راجع `PluginLink.h` و`PluginLTO.h` للتصريحات المعيارية، و
`Schema/PhaseSchema.json` لسياسات المراحل العشرين، و`coverage.json` للاختبارات
التي تثبّت كلًّا منها.
