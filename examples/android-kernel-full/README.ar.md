**اللغات**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# عرض SDK الكامل لنواة Android

تكامل SDK الكامل — يُهيئ جميع أنظمة NVK الفرعية ويعرضها عبر واجهة أوامر netlink. التنفيذ المرجعي لوحدات الإنتاج. يشمل: محرك Interpose، بيانات الاعتماد، إخفاء الوحدة، SELinux، تعداد العمليات، فحص VMA، إدخال/إخراج الملفات، كشف البيئة، والإحصائيات.

## البناء

```bash
cd examples/android-kernel-full
neverc make
```

غيّر `KERNEL` إلى `515` أو `601` أو `606` أو `612` لإصدارات أخرى.

## النشر والتشغيل

```bash
neverc make run
```

أو يدويًا:

```bash
adb push nvk_full.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_full.ko'
adb shell su -c 'dmesg | grep nvk_full'
```

## إلغاء التحميل

```bash
neverc make rmmod
```

أو يدويًا:

```bash
adb shell su -c 'rmmod nvk_full'
```
