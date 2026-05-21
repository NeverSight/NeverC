**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[→ نظام وقت التشغيل المدمج في NeverC](../README.ar.md)

# مخصص الذاكرة mimalloc المدمج

## نظرة عامة

يمكن لـ NeverC تضمين [mimalloc](https://github.com/microsoft/mimalloc) — مخصص الذاكرة عالي الأداء من Microsoft — مباشرة في الملفات الثنائية المترجمة عبر دمج LLVM bitcode. عند التفعيل، يتم استبدال `malloc` و `free` و `calloc` و `realloc` بشفافية بتطبيقات mimalloc أثناء الترجمة.

**التفعيل:**

```bash
neverc -fbuiltin-mimalloc main.c -o main
```

---

## دعم المنصات

| المنصة | Triple | الحالة |
|--------|--------|--------|
| Linux x86_64 | `x86_64-unknown-linux-gnu` | مدعوم |
| Linux AArch64 | `aarch64-unknown-linux-gnu` | مدعوم |
| Android | `aarch64-linux-android` | مدعوم |
| macOS x86_64 | `x86_64-apple-macosx` | مدعوم |
| macOS AArch64 | `arm64-apple-macosx` | مدعوم |
| iOS | `arm64-apple-ios` | مدعوم |
| Windows x86_64 (MSVC) | `x86_64-pc-windows-msvc` | مدعوم |
| Windows AArch64 (MSVC) | `aarch64-pc-windows-msvc` | مدعوم |

---

## الكبت التلقائي

| العلم / الوضع | السبب |
|---------------|-------|
| `-fno-builtin` | لا سيناريو تجاوز CRT |
| `-mkernel` | لا كومة مساحة المستخدم في النواة |
| `-fshellcode-mode` | لا كومة في الشلكود |
| `-ffreestanding` | لا libc للتجاوز |

---

## عملية Bootstrap

```bash
ninja neverc                         # المرحلة 1: عناصر نائبة bitcode فارغة
ninja neverc-bootstrap-mimalloc-bc   # المرحلة 2: ترجمة bitcode لكل نظام تشغيل
ninja neverc                         # المرحلة 3: تضمين bitcode الحقيقي
```

---

## مرجع أعلام المترجم

| العلم | الوصف |
|-------|-------|
| `-fbuiltin-mimalloc` | تفعيل حقن تجاوز mimalloc (معطل افتراضياً) |
| `-fno-builtin-mimalloc` | تعطيل حقن mimalloc صراحة |

| الماكرو | القيمة | متى يُعرَّف |
|---------|--------|-------------|
| `__NEVERC_MIMALLOC__` | `1` | عندما يكون `-fbuiltin-mimalloc` نشطاً |
