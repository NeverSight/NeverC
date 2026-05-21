**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[→ وثائق NeverC](../README.ar.md)

# نظام وقت التشغيل المدمج في NeverC

يوسع NeverC لغة C القياسية بأوقات تشغيل مدمجة اختيارية، مضمنة مباشرة في ملف المترجم الثنائي كـ LLVM bitcode. عند تفعيلها عبر أعلام المترجم، يتم دمج وقت التشغيل المقابل في IR المستخدم أثناء الترجمة — دون الحاجة لملفات رأسية خارجية أو مكتبات أو تبعيات ربط.

## الميزات المدمجة المتاحة

| المدمج | العلم | الافتراضي | الوصف |
|--------|-------|-----------|-------|
| [**`string`**](string/README.ar.md) | `-fbuiltin-string` | معطل | نوع سلسلة نصية بدلالة القيمة مع أساليب النقطة، إدارة تلقائية للذاكرة ودعم UTF-8 أصلي |
| [**`mimalloc`**](mimalloc/README.ar.md) | `-fbuiltin-mimalloc` | **مفعّل** | مخصص ذاكرة عالي الأداء يستبدل بشفافية `malloc`/`free`/`calloc`/`realloc` |

```bash
neverc -fbuiltin-string -fbuiltin-mimalloc main.c -o main
```

---

## نظرة عامة على البنية

تشترك جميع الميزات المدمجة في نفس البنية ذات الأربع طبقات:

1. **خيارات اللغة وأعلام المشغل** — `LangOption` معرف في `LangOptions.def`
2. **واجهة Foundation** — توفر `getEmbeddedBitcode()` و `isSupported()`
3. **بنية CMake Bootstrap التحتية** — توليد bitcode على مرحلتين
4. **مسار دمج IR** — دمج bitcode في وحدة المستخدم عند `PipelineStartEP`

---

## الاختلافات التصميمية بين المدمجات

| الجانب | `string` | `mimalloc` |
|--------|----------|------------|
| **استراتيجية الدمج** | حسب الطلب (BFS رسم بياني للاستدعاءات) | أرشيف كامل (جميع الرموز) |
| **bitcode المنصة** | واحد (مستقل عن البنية) | لكل نظام تشغيل (Linux / Darwin / Windows) |
| **معالجة الرموز** | الكل مُداخل | نقاط دخول التجاوز تحتفظ بالربط الخارجي |
| **ماكرو المعالج المسبق** | *(لا يوجد)* | `__NEVERC_MIMALLOC__` |
| **وضع الشلكود** | تفعيل تلقائي، إعادة كتابة الحلبة | مكبوت (لا كومة في الشلكود) |
| **مستوى التحسين** | `-O0` (ترجمة bitcode) | `-O2` (مخصص حرج الأداء) |
| **DCE** | تقليم قبل الدمج + مسح بعد الدمج | بدون DCE (دلالات الأرشيف الكامل) |

---

## أقفال الأمان

| الشرط | التأثير | السبب |
|-------|---------|-------|
| `-fno-builtin` | يكبت mimalloc | لا سيناريو تجاوز CRT |
| `-mkernel` | يكبت mimalloc | لا كومة مساحة المستخدم في النواة |
| `-fshellcode-mode` | يكبت mimalloc | لا كومة في الشلكود |
| `-ffreestanding` | يكبت mimalloc | لا libc للتجاوز |

---

## ماكروهات المعالج المسبق

```c
#ifdef __NEVERC_MIMALLOC__
// mimalloc نشط — malloc/free يتم تجاوزهما بشفافية
#endif
```

---

## هيكل الملفات

```
neverc/
├── include/neverc/Foundation/Builtin/
│   ├── BuiltinString.h / BuiltinMimalloc.h
├── lib/Foundation/Builtin/
│   ├── BuiltinString.cpp / BuiltinMimalloc.cpp
│   ├── bin2c.py / gen_string_runtime.py / gen_mimalloc_source.py
├── lib/Emit/Backend/
│   ├── BackendUtil.cpp / StringRuntimeLinker.{h,cpp} / MimallocRuntimeLinker.{h,cpp}
├── lib/Invoke/ToolChains/NeverC.cpp
└── lib/Compiler/Preprocessor/InitPreprocessor.cpp
```

---

## إضافة ميزة مدمجة جديدة

1. إضافة `LANGOPT` في `LangOptions.def`
2. إضافة أعلام المشغل في `Options.td.h`
3. إنشاء واجهة Foundation (`BuiltinFoo.h` + `.cpp`)
4. إنشاء مولد الشفرة المصدرية
5. إضافة أهداف CMake bootstrap
6. إنشاء مسار IR وتسجيله في `PipelineStartEP`
7. تعريف ماكرو المعالج المسبق
8. إضافة فحوصات الأمان
9. إضافة الاختبارات
10. إضافة التوثيق وترجمات i18n
