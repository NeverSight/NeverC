<div dir="rtl">

**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[→ وثائق NeverC](../README.ar.md)

# نظام وقت التشغيل المدمج في NeverC

يوسع NeverC لغة C القياسية بأوقات تشغيل مدمجة اختيارية، مضمنة مباشرة في ملف المترجم الثنائي كـ LLVM bitcode. عند تفعيلها عبر أعلام المترجم، يتم دمج وقت التشغيل المقابل في IR المستخدم أثناء الترجمة — دون الحاجة لملفات رأسية خارجية أو مكتبات أو تبعيات ربط.

## الميزات المدمجة المتاحة

| المدمج | العلم | الافتراضي | الوصف |
|--------|-------|-----------|-------|
| [**`string`**](string/README.ar.md) | `-fbuiltin-string` | معطل | نوع سلسلة نصية بدلالة القيمة مع أساليب النقطة، إدارة تلقائية للذاكرة ودعم UTF-8 أصلي |
| [**`mimalloc`**](mimalloc/README.ar.md) | `-fbuiltin-mimalloc` | **مفعّل** | مخصص ذاكرة عالي الأداء يستبدل بشفافية `malloc`/`free`/`calloc`/`realloc` |
| [**`xorstr`**](xorstr/README.ar.md) | `-fencrypt-call-strings` | معطّل | تشفير مستقل لكل مثيل، ختم متأخر إلزامي، توسيع عند كل موضع استدعاء وتنظيف متطاير للمكدس |
| [**`strhash`**](strhash/README.ar.md) | `-fstrhash-algo` / `-fstrhash-fold` | معطّل | تجزئة السلاسل وقت الترجمة بنفس الخوارزمية وقت التشغيل، طي IR اختياري |

```bash
neverc -fbuiltin-string -fbuiltin-mimalloc main.c -o main
```

---

## نظرة عامة على البنية

يشترك `string` و `mimalloc` في نفس البنية ذات الأربع طبقات:

1. **خيارات اللغة وأعلام المشغل** — `LangOption` معرف في `LangOptions.def`
2. **واجهة Foundation** — توفر `getEmbeddedBitcode()` و `isSupported()`
3. **بنية CMake Bootstrap التحتية** — توليد bitcode على مرحلتين
4. **مسار دمج IR** — دمج bitcode في وحدة المستخدم عند `PipelineStartEP`

مثال على التسجيل في `LangOptions.def`:

```cpp
LANGOPT(BuiltinString,      1, 0, "inject NeverC builtin string prelude")
LANGOPT(BuiltinMimalloc,    1, 1, "inject mimalloc allocator override")
LANGOPT(EncryptCallStrings, 1, 0, "auto-encrypt string literals in call arguments")
VALUE_LANGOPT(EncryptCallStringsMaxLen, 32, 1024,
              "maximum string length for auto-encryption (0 = no limit)")
```

> **ملاحظة:** لا يستخدم `xorstr` نموذج bitcode المضمّن. يخفض `semaBuiltinNeverCXorstr` في `SemaCheckingBuiltinNeverC.cpp` الماكرو الصريح. يختم `EncryptCallStringsPass` و`XorStrCleanupPass` الـ literals التلقائية ومخازن النص الصريح قبل IPO وبعد كل مرحلة IR متأخرة عادية أو خاصة بـ plugin. لا يعيد `FinalizeXorStrPass` تشفير المفككات الصريحة ويوسعها إلا عند حد حقيقي للشيفرة الآلية الأصلية، ثم يحذف رسم المساعدات المشترك. تصف [وثائق xorstr](xorstr/README.ar.md) التصميم وعقد قابلية إعادة البناء.

> **ملاحظة:** `strhash` لا يستخدم أيضاً نموذج bitcode المضمّن. [`NC_STRHASH(s)`](strhash/README.ar.md) يُطوى إلى ثابت في Sema؛ `-fstrhash-fold` يفعّل `StrHashFoldPass`. انظر [وثائق strhash](strhash/README.ar.md).


---

## الاختلافات التصميمية بين المدمجات

| الجانب | `string` | `mimalloc` |
|--------|----------|------------|
| **استراتيجية الدمج** | حسب الطلب (BFS رسم بياني للاستدعاءات) | أرشيف كامل (جميع الرموز) |
| **bitcode المنصة** | واحد (مستقل عن البنية) | لكل نظام تشغيل (Linux / Darwin / Windows) |
| **معالجة الرموز** | الكل مُداخل | نقاط دخول التجاوز تحتفظ بالربط الخارجي |
| **ماكرو المعالج المسبق** | *(لا يوجد)* | `__NEVERC_MIMALLOC__` |
| **وضع الشلكود** | تفعيل تلقائي، إعادة كتابة الحلبة | مكبوت (HeapArenaPass يتولى الكومة) |
| **مستوى التحسين** | `-O0` (ترجمة bitcode) | `-O2` (مخصص حرج الأداء) |
| **DCE** | تقليم قبل الدمج + مسح بعد الدمج | بدون DCE (دلالات الأرشيف الكامل) |

---

## أقفال الأمان

| الشرط | التأثير | السبب |
|-------|---------|-------|
| `-fno-builtin` | يكبت mimalloc | لا سيناريو تجاوز CRT |
| `-mkernel` | يكبت mimalloc | لا كومة مساحة المستخدم في النواة |
| `-fdyncode-mode` | يكبت mimalloc | يُستبدل بـ HeapArenaPass (قائم على الساحة) |
| `-ffreestanding` | يكبت mimalloc | لا libc للتجاوز |

المكوّن المدمج `string` له منطق كبت خاص به (إعادة كتابة الساحة في خط أنابيب الشلكود تستبدل تخصيص الكومة).

### HeapArenaPass (تخصيص كومة الشلكود)

عند تفعيل `-fdyncode-mode`، يُكبت `mimalloc` لكن استدعاءات `malloc`/`free`/`calloc`/`realloc` تُعاد كتابتها تلقائياً بواسطة `HeapArenaPass` (مفعّل افتراضياً). يستخدم هذا التمرير استراتيجية هجينة:

- **التخصيصات الصغيرة (≤ 64 كيلوبايت)**: تُخدم من ساحة مقيمة على المكدس مشتركة مع وقت تشغيل `string` المدمج (مخصص bump + إعادة استخدام قائمة حرة).
- **التخصيصات الكبيرة (> 64 كيلوبايت) أو نفاد ساحة**: التراجع إلى مخصص نظام التشغيل:
  - **Windows**: `malloc`/`free` تُحلّ من `msvcrt.dll` عبر PEB walk (`-mdyncode-win-peb-import`).
  - **Linux / macOS / Android**: `mmap`/`munmap` تُضمّن كاستدعاءات نظام أصلية (`-mdyncode-syscall`).
  - **لا تمرير استيراد مفعّل**: ساحة فقط؛ نفاد الذاكرة يُرجع `NULL`.

التحكم عبر أعلام المشغّل:

```bash
neverc -fdyncode test.c -o test.bin                     # HeapArenaPass مفعّل (افتراضي)
neverc -fdyncode -fno-dyncode-heap-arena test.c       # HeapArenaPass معطّل (السلوك الأصلي)
```

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
│   └── Builtins.def                      # __builtin_neverc_xorstr / strhash
├── include/neverc/Transforms/XorStr/
│   └── EncryptCallStringsPass.h / XorStrCleanupPass.h
├── include/neverc/Transforms/StrHash/    # strhash
│   └── StrHashFoldPass.h / StrHashCompute.h
├── lib/Foundation/Builtin/
│   ├── BuiltinString.cpp / BuiltinMimalloc.cpp
│   └── bin2c.py / gen_string_runtime.py / gen_mimalloc_source.py
├── lib/Headers/neverc/
│   ├── xorstr/xorstr.h / xorstr/xorstr_impl.inc # ماكرو NC_XORSTR / NEVERC_XORSTR
│   └── strhash.h / strhash_impl.inc        # ماكرو NC_STRHASH / NC_STRHASH_AUTO
├── lib/Analyze/Checking/SemaCheckingBuiltinNeverC.cpp # semaBuiltinNeverCXorstr
├── lib/Transforms/XorStr/
│   └── EncryptCallStringsPass.cpp / XorStrCleanupPass.cpp
├── lib/Emit/Backend/
│   └── BackendUtil.cpp / StringRuntimeLinker.{h,cpp} / MimallocRuntimeLinker.{h,cpp}
├── lib/Invoke/ToolChains/NeverC.cpp
└── lib/Compiler/Preprocessor/InitPredefinedMacros.cpp
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

</div>
