<div dir="rtl">

**اللغات**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← أمثلة NeverC](../../docs/examples/README.ar.md)

# Android Kernel Multi-File Module

وحدة نواة NeverC متعددة الملفات. النقاط الرئيسية:

- **تهيئة واحدة**: `NEVERC_KRT_BOOTSTRAP()` يُستدعى مرة واحدة فقط في `module_init`
- **حالة مشتركة**: يرفع المترجم كل حالة `neverc_krt_*` إلى ربط `weak_odr`، جميع ملفات `.c` تتشارك نفس المحلل والذاكرة المؤقتة وحالة الأنظمة الفرعية
- **بنية مقسمة**: `main.c` (التهيئة/الخروج)، `interposes.c` (منطق الربط)، `utils.c` (المساعدات)

## البناء

```bash
cd examples/android-kernel-multifile
neverc make
```

غيّر `KERNEL` إلى `515` أو `601` أو `606` أو `612` لإصدارات نواة أخرى.

## النشر والتشغيل

```bash
neverc make run
```

أو يدوياً:

```bash
adb push nvk_multi.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_multi.ko'
adb shell su -c 'dmesg | grep neverc_krt_multi'
```

## إلغاء التحميل

```bash
neverc make rmmod
```

</div>
