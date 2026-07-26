<div dir="rtl">

**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← أمثلة NeverC](../../docs/examples/README.ar.md)

# مثال مكتبة مشتركة Android

مكتبة مشتركة `.so` أصلية ARM64 مُترجمة تبادلياً لـ Android باستخدام NeverC. يمكن البناء من macOS أو Windows أو Linux.

## البناء

```bash
cd examples/android-so
neverc make
```

## البناء اليدوي

```bash
neverc --target=aarch64-linux-android -Wall -shared -fPIC -ldl -o libneverc.so lib.c
```

## الميزات

- وظائف مساعدة لأبحاث أمان الألعاب: استعلام PID، قراءة `/proc/self/maps`، تخصيص ذاكرة RWX، تشفير XOR
- تحميل ديناميكي لـ `liblog.so` عبر `dlopen`
- عرض تخصيص ذاكرة قابلة للتنفيذ مع `mmap` + `PROT_EXEC`

</div>
