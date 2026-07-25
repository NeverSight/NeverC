**اللغات**: [English](dyncode.md) | [简体中文](dyncode.zh-CN.md) | [繁體中文](dyncode.zh-TW.md) | [日本語](dyncode.ja.md) | [한국어](dyncode.ko.md) | [Français](dyncode.fr.md) | [Deutsch](dyncode.de.md) | [Español](dyncode.es.md) | [Italiano](dyncode.it.md) | [Русский](dyncode.ru.md) | [العربية](dyncode.ar.md)

# إضافات DynCode

يُصرِّف `-fdyncode` وحدة ترجمة واحدة إلى صورة مسطّحة مستقلة عن الموضع (`.bin`)
لا تحمل شيفرتها أي انتقالات عنونة ولا تملك مقطع بيانات. وهي تستهدف arm64/x86_64
على macOS وLinux وAndroid وWindows، عند مستوى تنفيذ المستخدم أو النواة. تراقب
الإضافات المراحل المُصنَّفة التي تحوّل C إلى تلك الصورة، أو تعترضها، أو تستبدلها،
عبر واجهة C الخالصة نفسها التي تستعملها بقية المجالات: بلا كائنات C++ من LLVM،
ولا أنواع STL، ولا استثناءات، ولا مؤشرات مضيف لا تُصرّح جداول الواجهات بمدة
حياتها.

## الواجهات

```c
#include "neverc/Plugin/PluginDynCode.h"
```

| الواجهة | الجدول | الفتحات | الغرض |
|---|---|--:|---|
| `NEVERC_INTERFACE_DYNCODE_{HIGH,LOW}` | `NevercDynCodeAPI` | 16 | قراءة الطلب والصورة والتقرير وخرائط المقاطع والرموز والانتقالات والمراجع الخارجية |
| `NEVERC_INTERFACE_DYNCODE_REGISTRAR_{HIGH,LOW}` | `NevercDynCodeRegistrarAPI` | 5 | `RegisterTarget` و`RegisterImportProvider` و`RegisterExtractor` و`RegisterCharsetEncoder` و`RegisterBinaryVerifier` |
| `NEVERC_INTERFACE_DYNCODE_PHASE_{HIGH,LOW}` | `NevercDynCodePhaseAPI` | 4 | `GetPhaseInfo` و`GetRequest` و`GetImage` و`GetReport` |

الثلاث جميعها `NEVERC_INTERFACE_STABLE` عند الإصدار الرئيسي 1. ومن داخل نداء
مرحل