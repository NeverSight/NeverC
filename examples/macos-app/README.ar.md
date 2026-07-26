<div dir="rtl">

**اللغات**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← أمثلة NeverC](../../docs/examples/README.ar.md)

# مثال تطبيق macOS

ملف تنفيذي أصلي بتنسيق macOS Mach-O تم تجميعه بشكل متقاطع باستخدام NeverC. يوضح استخدام sysctl و uname وواجهات برمجة نواة Mach لفحص معلومات النظام والعمليات. يمكن البناء من macOS أو Windows أو Linux — بدون الحاجة إلى Xcode.

## البناء

من المستودع (الهدف الافتراضي: `arm64-apple-macos`):

```bash
cd examples/macos-app
neverc make
```

البناء لمعالج Intel:

```bash
neverc make TARGET=x86_64-apple-macos
```

باستخدام إصدار مستقل من NeverC:

```bash
neverc make NEVERC=/path/to/neverc
```

## البناء اليدوي (بدون Make)

```bash
neverc --target=arm64-apple-macos -Wall -o macos-app main.c
```

## التشغيل

```bash
./macos-app
```

## الميزات

- استعلام معلومات النواة عبر `uname`
- قراءة تفاصيل العتاد عبر `sysctl` (الطراز، عدد المعالجات، حجم الذاكرة، حجم الصفحة)
- عرض هوية العملية (`getpid`، `getppid`، `getuid`)
- استرجاع معلومات مضيف Mach (`host_info`) وإحصائيات ذاكرة المهمة (`task_info`)

</div>
