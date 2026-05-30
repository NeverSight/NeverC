**اللغات**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# واجهة برمجة الإضافات الخارجية لـ NeverC

يوفر NeverC **واجهة C ABI نقية** لإضافات المرور الخارجية (out-of-tree). الإضافة هي مكتبة مشتركة (`.dll` / `.so` / `.dylib`) تسجّل مرورات مخصصة في نقاط محددة من خط أنابيب التجميع. تُبنى الإضافة باستخدام **ملف رأس واحد** (`NevercPluginAPI.h`) **بدون** أي اعتمادية على LLVM أو CRT — جميع الوظائف تُمرر عبر جدول vtable الذي يوفره المضيف.

## 1. بداية سريعة

### إضافة بسيطة

```c
#include "neverc/Plugin/NevercPluginAPI.h"

static int myPass(NevercModuleRef M, const NevercHostAPI *API, void *UD) {
    (void)UD;
    unsigned Count = 0;
    NEVERC_FOR_EACH_DEFINED_FUNCTION(API, M, F) {
        (void)F;
        Count++;
    }
    API->DiagNoteF("[my-plugin] %u defined functions", Count);
    return 0;
}

static void registerPasses(const NevercHostAPI *API, void *Reg) {
    API->RegisterModulePass(Reg, NEVERC_HOOK_PRE_OPT, myPass, NULL, "my-pass");
}

NEVERC_EXPORT NevercPluginInfo nevercGetPluginInfo(void) {
    NevercPluginInfo Info;
    Info.APIVersion     = NEVERC_PLUGIN_API_VERSION;
    Info.PluginName     = "my-plugin";
    Info.PluginVersion  = "1.0.0";
    Info.RegisterPasses = registerPasses;
    Info.Destroy        = NULL;
    return Info;
}
```

### البناء

```bash
cc -shared -o MyPlugin.dll MyPlugin.c -I/path/to/pluginsdk/include
```

### التشغيل

```bash
neverc -fplugin-pass=./MyPlugin.dll input.c -o output.obj
```

## 2. الهيكل

- **SDK بملف رأس واحد**: يحتاج فقط `NevercPluginAPI.h` لبناء إضافة.
- **بدون اعتماديات**: لا حاجة لملفات LLVM الرأسية ولا ربط CRT. جميع العمليات عبر vtable.
- **واجهة C ABI نقية**: يمكن كتابة الإضافات بلغة C أو C++ أو Zig أو Rust (FFI) أو أي لغة تنتج مكتبة مشتركة بربط C.
- **أمان الإصدارات**: استخدم `NEVERC_API_FN(api, Field)` للتحقق من إدخالات vtable الاختيارية قبل استدعائها.

## 3. نقطة الدخول

```c
NEVERC_EXPORT NevercPluginInfo nevercGetPluginInfo(void);
```

| الحقل | النوع | الوصف |
|-------|-------|-------|
| `APIVersion` | `uint32_t` | يجب أن يكون `NEVERC_PLUGIN_API_VERSION` |
| `PluginName` | `const char *` | اسم مقروء |
| `PluginVersion` | `const char *` | سلسلة إصدار دلالية |
| `RegisterPasses` | مؤشر دالة | يُستدعى مرة واحدة لتسجيل جميع المرورات |
| `Destroy` | مؤشر دالة | تنظيف اختياري، يمكن أن يكون `NULL` |

## 4. أنواع المرورات

- **Module Pass (IR)**: يعمل على وحدة LLVM IR.
- **Machine Pass (MIR)**: يعمل على IR مستوى الآلة.
- **Binary Pass**: يعمل على البايتات الخام.
- **Linker Pass**: يعمل في وقت الربط.

## 5. نقاط الخطاف

| الخطاف | المستوى | الوصف |
|--------|---------|-------|
| `NEVERC_HOOK_PRE_OPT` | IR | قبل تحسينات LLVM |
| `NEVERC_HOOK_POST_OPT` | IR | بعد تحسينات LLVM |
| `NEVERC_HOOK_PIPELINE_START` | IR | بداية خط الأنابيب |
| `NEVERC_HOOK_PIPELINE_LAST` | IR | نهاية خط أنابيب IR |
| `NEVERC_HOOK_SC_*` | IR/MIR/ثنائي | تدفق shellcode |
| `NEVERC_HOOK_LTO_*` | IR | تدفق LTO |
| `NEVERC_HOOK_LINK_*` | الرابط | تدفق الرابط |

## 6. أنواع المقابض المبهمة

جميع كائنات IR/MIR يتم الوصول إليها عبر مقابض مبهمة: `NevercModuleRef`، `NevercValueRef`، `NevercBasicBlockRef`، `NevercTypeRef`، `NevercBuilderRef`، `NevercContextRef`، `NevercMetadataRef`، `NevercNamedMDRef`، `NevercComdatRef`، `NevercMachineFuncRef`، `NevercMachineBBRef`، `NevercMachineInstrRef`، `NevercUseRef`، `NevercLinkerSymbolRef`، `NevercLinkerSectionRef`. المقابض صالحة **فقط ضمن نطاق استدعاء المرور** الذي استلمها.

## 7. هياكل البيانات

**Arena** (مخصص bump-pointer)، **StrMap** (جدول تجزئة بمفتاح نصي)، **IntMap** (جدول تجزئة بمفتاح صحيح)، **StrBuilder** (بناء نصوص تراكمي)، **ValueSet** (مجموعة تجزئة للقيم).

## 8. توافق الإصدارات

```c
if (NEVERC_API_FN(API, SomeNewFunction)) {
    API->SomeNewFunction(...);
}
```

## 9. معاملات الإضافة

```bash
neverc -fplugin-pass=./MyPlugin.dll \
       -fplugin-pass-arg=verbose=1 \
       input.c -o output.obj
```

## 10. أفضل الممارسات

1. **Arena أولاً**: استخدم `NEVERC_TRY_ARENA` للبيانات المؤقتة.
2. **حماية الإصدار**: لف استدعاءات vtable الجديدة دائمًا بـ `NEVERC_API_FN`.
3. **تكرار بالاستدعاء الخلفي**: `ModuleForEachDefinedFunction` أسرع من الماكرو.
4. **بدون اعتمادية CRT**: جميع العمليات عبر vtable.
5. **عودة نظيفة**: حرر جميع الموارد قبل العودة من المرور.

## 11. محتويات Plugin SDK

```
pluginsdk/
├── include/
│   └── neverc/
│       └── Plugin/
│           └── NevercPluginAPI.h
└── examples/
    ├── Makefile
    ├── ExamplePlugin.c
    ├── CrtShimPlugin.c
    └── BenchPlugin.c
```

## 12. وثائق ذات صلة
