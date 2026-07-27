<div dir="rtl">

**اللغات**: [English](prep.md) | [简体中文](prep.zh-CN.md) | [繁體中文](prep.zh-TW.md) | [日本語](prep.ja.md) | [한국어](prep.ko.md) | [Français](prep.fr.md) | [Deutsch](prep.de.md) | [Español](prep.es.md) | [Italiano](prep.it.md) | [Русский](prep.ru.md) | [العربية](prep.ar.md)

[← واجهة NeverC الثنائية للإضافات](README.ar.md)

# واجهة المعالج المسبق لإضافات NeverC

تكشف [`PluginPrep.h`] المعالج المسبق بطريقتين. **الاشتراك** في 39 نوعًا من الأحداث
يمنحك أثرًا للقراءة فقط لكل ما يفعله المعالج المسبق: دخول الملفات، وتعريف
وحدات الماكرو وتوسيعها، وتقييم الشروط، والـ pragma. أما المراحل الست **فتمضي
أبعد** وتتيح لك إعادة كتابة النتيجة: إعادة توجيه `#include`، أو استبدال رموز
توسيع ماكرو، أو معالجة pragma بنفسك، أو الإجابة عن `__has_feature` إجابةً
مختلفة.

## الواجهة

```c
#include "neverc/Plugin/PluginPrep.h"

Bootstrap->QueryInterface(
    Bootstrap->Context,
    (NevercInterfaceID){NEVERC_INTERFACE_PREP_HIGH, NEVERC_INTERFACE_PREP_LOW},
    NEVERC_PREP_API_MAJOR, NEVERC_PREP_API_MINOR, &Table, &Minor, &TableSize);
```

أنواع الرموز الـ230 (`NEVERC_TOKEN_KIND_COUNT`) وأنواع الكلمات المفتاحية للمعالج
المسبق تأتي من [`Schema/PluginPrepSchema.inc`] الذي يضمّه الملف الترويسي، ويجب أن
يساوي رقمه الرئيسي للقدرات `NEVERC_PREP_API_MAJOR` — فأي اختلاف خطأ ترجمة، لا
مفاجأة وقت التشغيل. ويحمل كل نوع كذلك فئةً: `NEVERC_TOKEN_CATEGORY_SPECIAL`
و`COMMENT` و`IDENTIFIER` و`LITERAL` و`PUNCTUATOR` و`KEYWORD` و`ANNOTATION`.

## مراحل المعالج المسبق الست

| المرحلة | السياسة | الدخل ← الخرج |
|---|---|---|
| `neverc.prep.token` | OBSERVABLE، INTERCEPTABLE، REPLACEABLE | رمز واحد ← قائمة رموز |
| `neverc.prep.build_token_stream` | مثلها | مدى ← دفق رموز |
| `neverc.prep.include.intercept` | مثلها | طلب تضمين ← قرار تضمين |
| `neverc.prep.macro.intercept` | مثلها | عملية ماكرو ← إجراء + رموز |
| `neverc.prep.pragma.intercept` | مثلها | pragma ← إجراء + رموز |
| `neverc.prep.feature_query.intercept` | مثلها | استعلام `__has_*` ← قيمة |

لخمس من الست زوج `Get<Kind>PhaseInput` و`Create<Kind>PhaseOutput` على
`NevercPrepAPI`، ويأخذ نصف `Create` قيمة `NevercPhaseContinuation` الخاصة
بالمعترِض، فلا يمكن إنتاج خرج إلا من داخل المرحلة التي تملكه.
`neverc.prep.build_token_stream` استثناء: لديها `GetTokenStreamPhaseInput`
وتنشر عبر `TokenStreamBuilderCommit` على `Frame` المرحلة، لا عبر
`Create*PhaseOutput` يأخذ continuation.

## قراءة الرموز

```c
typedef struct NevercTokenInfo {
  NevercABITableHeader Header;
  NevercTokenKind Kind;
  NevercTokenFlags Flags;
  NevercTokenOriginKind Origin;
  uint32_t Reserved;
  NevercStringView Spelling;
  NevercSourceLocation Location;
  NevercSourceRange Range;
  NevercIdentifierHandle Identifier;
  NevercMacroDefinitionHandle MacroDefinition;
} NevercTokenInfo;
```

`Origin` إما `NEVERC_TOKEN_ORIGIN_FILE` أو `MACRO_REPLACEMENT` أو
`MACRO_ARGUMENT` أو `SYNTHESIZED`، وبها تميّز رمزًا كتبه المستخدم عن رمز أنتجه
ماكرو.

أما الرايات فهي دفاتر المعالج المسبق نفسه، وتصبح مهمة حين تُركّب رموزًا:

| الراية | المعنى |
|---|---|
| `NEVERC_TOKEN_FLAG_START_OF_LINE` | أول رمز في سطره |
| `_LEADING_SPACE` | يسبقه فراغ |
| `_DISABLE_EXPANSION` | لا توسّع هذا الرمز بالماكرو |
| `_NEEDS_CLEANING` | الكتابة تحوي أسطرًا مهروبة أو ثلاثيات محارف |
| `_LEADING_EMPTY_MACRO` | توسّع قبله مباشرةً ماكرو فارغ |
| `_HAS_UCN` | يحوي اسم محرف عالميًا |
| `_IGNORED_COMMA`، `_COMMA_AFTER_ELIDED` | دفاتر حذف الفاصلة في المتغيّرة الوسائط |
| `_STRINGIFIED_IN_MACRO` | أنتجه المعامل `#` |
| `_REINJECTED` | أُعيد حقنه في دفق الرموز |

و`NEVERC_TOKEN_FLAG_ALL` قناع كل البتات المعرَّفة. القراءات الدُفعية تستعمل
`GetTokenInfoBatch`، ويُقرأ الدفق كاملًا إما كرؤية خفيفة لسجلات
`NevercTokenView` عبر `GetTokenStreamView`، أو مقبضًا مقبضًا عبر
`GetTokenStreamToken`. ويسع الدفق الواحد على الأكثر
`NEVERC_PREP_TOKEN_STREAM_MAX_TOKENS` (16,777,216) رمزًا.

## المعرّفات ووحدات الماكرو

```c
NevercIdentifierHandle Identifier;
Prep->GetOrCreateIdentifier(Prep->Context, Task, SV("MY_MACRO"), &Identifier);

NevercMacroDefinitionHandle Definition;
Prep->GetMacroDefinitionForIdentifier(Prep->Context, Task, Identifier,
                                      &Definition);

NevercMacroDefinitionInfo Info = {0};
Info.Header = (NevercABITableHeader){sizeof(Info), NEVERC_PREP_API_MAJOR,
                                     NEVERC_PREP_API_MINOR, 0};
Prep->GetMacroDefinitionInfo(Prep->Context, Task, Definition, &Info);
```

تبلّغ `NevercMacroDefinitionInfo` عن الاسم، والتوجيه المعرِّف، ومواضع التعريف
والنهاية وإلغاء التعريف، وعدد المعاملات ورموز الاستبدال، والرايات:
`NEVERC_MACRO_FUNCTION_LIKE` و`VARIADIC` و`C99_VARIADIC` و`GNU_VARIADIC`
و`HAS_VA_OPT` و`BUILTIN` و`COMMA_PASTING`. أما المعاملات ورموز الاستبدال فرادى
فتأتي من `GetMacroParameter` و`GetMacroReplacementToken`.

وتضيف `NevercIdentifierInfo` نوع الرمز، ونوع الكلمة المفتاحية للمعالج المسبق،
ومعرّف الدالة المدمجة، ورايات مثل `NEVERC_IDENTIFIER_KEYWORD` و`_HAS_MACRO`
و`_POISONED` و`_RESERVED`.

وعند موضع التوسيع تبلّغ `GetMacroArgumentInfo` عن عدد الوسائط وعمّا إذا حُذفت
الوسائط المتغيّرة، وتعطي `GetMacroArgumentTokenStream` رموز كل وسيط.

## الاشتراك في الأحداث

ردّ نداء واحد يستقبل جميع الأحداث المشترَك فيها. ويُبنى القناع من أنواع الأحداث
التي تهمّك:

```c
static NevercStatus NEVERC_CALL
on_event(NevercTaskHandle Task, const NevercPrepEvent *Event, void *UserData) {
  switch (Event->Kind) {
  case NEVERC_PREP_EVENT_INCLUSION_DIRECTIVE:
    /* Event->Payload.Include.Filename و.IsAngled و.File و.FilenameRange */
    break;
  case NEVERC_PREP_EVENT_MACRO_EXPANDS:
    /* Event->Payload.Macro.NameToken و.Definition و.Arguments و.Range */
    break;
  case NEVERC_PREP_EVENT_IFDEF:
    /* Event->Payload.Condition.Value هي NOT_EVALUATED أو FALSE أو TRUE */
    break;
  default:
    break;
  }
  return neverc_status_ok();
}

NevercPrepObserverDescriptor Observer = {0};
Observer.Header = (NevercABITableHeader){sizeof(Observer),
                                         NEVERC_PREP_API_MAJOR,
                                         NEVERC_PREP_API_MINOR, 0};
Observer.Events = NEVERC_PREP_EVENT_MASK(NEVERC_PREP_EVENT_INCLUSION_DIRECTIVE) |
                  NEVERC_PREP_EVENT_MASK(NEVERC_PREP_EVENT_MACRO_EXPANDS) |
                  NEVERC_PREP_EVENT_MASK(NEVERC_PREP_EVENT_IFDEF);
Observer.Callback = on_event;
Observer.UserData = State;
Prep->RegisterEventObserver(Prep->Context, Task, &Observer);
```

ويشترك `NEVERC_PREP_EVENT_MASK_ALL` في كل شيء. وهذه الأنواع الـ39 مجموعةً بحسب
عضو اتحاد الحمولة الذي تستعمله:

| الحمولة | الأحداث |
|---|---|
| `File` | `FILE_CHANGED`، `LEXED_FILE_CHANGED`، `FILE_SKIPPED`، `END_OF_MAIN_FILE` |
| `Include` | `INCLUSION_DIRECTIVE`، `FILE_NOT_FOUND`، `HAS_INCLUDE` |
| `Text` | `IDENT`، `PRAGMA_DIRECTIVE`، `PRAGMA_COMMENT`، `PRAGMA_MARK`، `PRAGMA_DETECT_MISMATCH`، `PRAGMA_DEBUG`، `PRAGMA_MESSAGE`، `PRAGMA_DIAGNOSTIC_PUSH`، `PRAGMA_DIAGNOSTIC_POP`، `PRAGMA_DIAGNOSTIC`، `PRAGMA_WARNING`، `PRAGMA_WARNING_PUSH`، `PRAGMA_WARNING_POP`، `PRAGMA_EXEC_CHARSET_PUSH`، `PRAGMA_EXEC_CHARSET_POP`، `PRAGMA_ASSUME_NONNULL_BEGIN`، `PRAGMA_ASSUME_NONNULL_END` |
| `Macro` | `MACRO_EXPANDS`، `MACRO_DEFINED`، `MACRO_UNDEFINED`، `DEFINED` |
| `Condition` | `IF`، `ELIF`، `IFDEF`، `ELIFDEF`، `ELIFDEF_SKIPPED`، `IFNDEF`، `ELIFNDEF`، `ELIFNDEF_SKIPPED`، `ELSE`، `ENDIF`، `SOURCE_RANGE_SKIPPED` |

وتميّز `NevercPrepFileEvent.Reason` بين `NEVERC_PREP_FILE_ENTER` و`EXIT`
و`SYSTEM_HEADER_PRAGMA` و`RENAME`. والأحداث للقراءة فقط: السجل وكل رؤية داخله
مستعارة طوال ردّ النداء، بينما تُرقّى المقابض المنشورة في حدثٍ ما إلى نطاق
المهمة المحيطة.

## إعادة توجيه تضمين

```c
NevercPrepIncludePhaseInput In = {0};
In.Header = (NevercABITableHeader){sizeof(In), NEVERC_PREP_API_MAJOR,
                                   NEVERC_PREP_API_MINOR, 0};
Prep->GetIncludePhaseInput(Prep->Context, Frame, Frame->Input, &In);

NevercPrepIncludePhaseOutput Out = {0};
Out.Header = In.Header;
if (view_equals(In.Filename, "legacy.h")) {
  Out.Action    = NEVERC_PREP_INCLUDE_REDIRECT;
  Out.Filename  = SV("modern.h");
  Out.IsAngled  = NEVERC_FALSE;
} else {
  Out.Action = NEVERC_PREP_INCLUDE_CONTINUE;
}

NevercArtifactHandle Output;
Prep->CreateIncludePhaseOutput(Prep->Context, Frame, Continuation, &Out,
                               &Output);
```

والإجراءات هي `NEVERC_PREP_INCLUDE_CONTINUE` و`_SKIP` و`_REDIRECT`. ويبلّغ
الدخل أيضًا عن `IsImport` و`IsIncludeNext`، فيمكن تمييز `#import` عن
`#include_next`.

## استبدال توسيع ماكرو

يحمل دخل مرحلة الماكرو العمليةَ الجارية — `NEVERC_PREP_MACRO_DEFINE` أو
`_UNDEFINE` أو `_EXPAND` أو `_EXPAND_BUILTIN` — مع رمز الاسم والتعريف والوسائط
ورموز الاستبدال التي كان المعالج المسبق سيستعملها.

```c
NevercPrepMacroPhaseOutput Out = {0};
Out.Header     = /* … */;
Out.Action     = NEVERC_PREP_MACRO_REPLACE_TOKENS;
Out.Tokens     = MyTokens;      /* const NevercTokenHandle * */
Out.TokenCount = MyTokenCount;
Prep->CreateMacroPhaseOutput(Prep->Context, Frame, Continuation, &Out, &Output);
```

يُبقي `NEVERC_PREP_MACRO_CONTINUE` السلوك المدمج، ويتوسّع `_SUPPRESS` إلى لا
شيء.

## بناء الرموز

تأتي الرموز المركَّبة من بانٍ يتحقق من تركيبة النوع والكتابة والمعرّف قبل
الإيداع:

```c
NevercTokenBuilderHandle Builder;
Prep->CreateTokenBuilder(Prep->Context, Task, &Builder);
Prep->TokenBuilderSetLiteral(Prep->Context, Task, Builder,
                             NEVERC_TOKEN_NUMERIC_CONSTANT, SV("42"));
Prep->TokenBuilderSetLocation(Prep->Context, Task, Builder, Location);
Prep->TokenBuilderSetFlags(Prep->Context, Task, Builder,
                           NEVERC_TOKEN_FLAG_LEADING_SPACE);

NevercTokenHandle Token;
Prep->TokenBuilderCommit(Prep->Context, Task, Builder, &Token);
Prep->DestroyTokenBuilder(Prep->Context, Task, Builder);
```

استعمل `TokenBuilderSetKind` لعلامات الترقيم والكلمات المفتاحية، و
`TokenBuilderSetIdentifier` للمعرّفات. وثوابت أنواع الرموز تأتي من
[`PluginPrepSchema.inc`].

وللدفق كاملًا — أي مرحلة `neverc.prep.build_token_stream` — راكِم في بانٍ للدفق
ثم أودِع مرةً واحدة:

```c
NevercTokenStreamBuilderHandle Stream;
Prep->CreateTokenStreamBuilder(Prep->Context, Task, &Stream);
Prep->TokenStreamBuilderAppend(Prep->Context, Task, Stream, Tokens, Count);
Prep->TokenStreamBuilderCommit(Prep->Context, Frame, Stream, &Output);
Prep->DestroyTokenStreamBuilder(Prep->Context, Task, Stream);
```

ويعطي دخل المرحلة `NevercPrepTokenStreamPhaseInput` موضعَي البداية والنهاية،
و`MaximumTokenCount` الذي يجب أن يحترمه الخرج.

## الـ pragma واستعلامات الخصائص

يبلّغ دخل مرحلة الـ pragma عن المُدخِل (`NEVERC_PREP_PRAGMA_HASH`، و`_OPERATOR`
لـ`_Pragma`، و`_MS` لـ`__pragma`)، وعن فضاء الاسم والاسم، وعن رموز الوسائط.
وإجراء الخرج هو `NEVERC_PREP_PRAGMA_CONTINUE` أو `_HANDLED` أو
`_REPLACE_TOKENS`.

ويغطي استعلام الخصائص كلًّا من `__has_feature` و`__has_extension`
و`__has_builtin` و`__has_include` و`__has_include_next` عبر
`NEVERC_PREP_QUERY_HAS_FEATURE` وأخواتها. يحمل الدخل الاسمَ و`BuiltinValue`
التي حسبها المترجم، ويكون الخرج إما متابعةً أو استبدالًا:

```c
NevercPrepFeatureQueryPhaseOutput Out = {0};
Out.Header = /* … */;
Out.Action = NEVERC_PREP_QUERY_REPLACE;
Out.Value  = NEVERC_TRUE;
Prep->CreateFeatureQueryPhaseOutput(Prep->Context, Frame, Continuation, &Out,
                                    &Output);
```

## القواعد

- سجلات الأحداث ورؤى السلاسل ومصفوفات الأعداد مستعارة طوال ردّ النداء. أما
  المقابض المنشورة في حدث فتعيش حتى تنتهي المهمة.
- كل بانٍ يحتاج نداء `Destroy*` المقابل له، حتى على مسار الخطأ.
- يتطلب نداء `Create<Kind>PhaseOutput` قيمة continuation الخاصة بالمرحلة التي
  ينتمي إليها؛ واستعمال continuation مرحلة أخرى يعيد
  `NEVERC_STATUS_WRONG_SCOPE`. أما `TokenStreamBuilderCommit` فيأخذ `Frame`
  مرحلة `build_token_stream` بدل continuation.
- اشترك فقط في الأحداث التي تعالجها. القناع هو صمّام الخنق — فالإضافة التي تأخذ
  `NEVERC_PREP_EVENT_MASK_ALL` ثم تُرشّح في لغة C تدفع ثمن كل ردّ نداء.
- تعمل ردود نداء المعالج المسبق على خيط المهمة بينما المعالج المسبق في وسط
  عمله. لا تعاود الدخول إليه من داخل أحدها.
- أعِد `NEVERC_STATUS_INVALID_ARGUMENT` عند غياب مؤشر مطلوب، ولا تدع استثناءً
  يعبر الحدّ أبدًا.

انظر [`PluginPrep.h`] و[`Schema/PluginPrepSchema.inc`] للتصريحات المعيارية، و
[`Schema/PrepSchema.json`] لمخطط أنواع الرموز، و[`Schema/PhaseSchema.json`]
لمراحل المعالِج المسبق الست وسياساتها.

<!-- reference links -->
[`PluginPrep.h`]: ../../neverc/include/neverc/Plugin/PluginPrep.h
[`PluginPrepSchema.inc`]: ../../neverc/include/neverc/Plugin/Schema/PluginPrepSchema.inc
[`Schema/PhaseSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PhaseSchema.json
[`Schema/PluginPrepSchema.inc`]: ../../neverc/include/neverc/Plugin/Schema/PluginPrepSchema.inc
[`Schema/PrepSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PrepSchema.json

</div>
