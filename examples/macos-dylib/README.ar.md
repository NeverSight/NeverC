<div dir="rtl">

**اللغات**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← أمثلة NeverC](../../docs/examples/README.ar.md)

# مثال مكتبة macOS الديناميكية

مكتبة ديناميكية أصلية بتنسيق macOS `.dylib` تم تجميعها بشكل متقاطع باستخدام NeverC. تغلف واجهات نواة Mach لفحص المهام وعمليات الذاكرة الافتراضية — مصممة لأبحاث الأمان. يمكن البناء من macOS أو Windows أو Linux — بدون الحاجة إلى Xcode.

## البناء

من المستودع (الهدف الافتراضي: `arm64-apple-macos`):

```bash
cd examples/macos-dylib
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
neverc --target=arm64-apple-macos -Wall -dynamiclib -o libneverc.dylib lib.c
```

## الميزات

- تصدير غلاف `nc_task_basic_info` لاستعلامات Mach `task_info`
- توفير `nc_vm_read`/`nc_vm_write` لقراءة/كتابة الذاكرة الافتراضية Mach
- `nc_vm_alloc`/`nc_vm_dealloc` لتخصيص وتحرير ذاكرة Mach VM
- دالة مساعدة لتشفير XOR واستعلامات PID/المهمة

</div>
