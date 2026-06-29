**اللغات**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android Kernel Function Hook

ربط `do_faccessat` عند نقطة الدخول باستخدام `neverc_krt_hook_register`. يوضح:

- **التسلسل التلقائي**: عدة معالجات على نفس الهدف، تُنفَّذ حسب الأولوية
- **نمط استدعاء الأصلي**: يستقبل المعالج مؤشر `orig` لاستدعاء الدالة الأصلية
- **التحكم بالأولوية**: قيمة أقل = تنفيذ أولاً؛ استخدم قيم سالبة للتنفيذ قبل الخطافات الأخرى
- **التعايش**: يعمل حتى لو كان الهدف مُربوطاً مسبقاً بواسطة وحدة أخرى

## API

```c
int neverc_krt_hook_register(void *target, void *handler, int priority,
                             void **orig, struct neverc_krt_hook_ref *ref);
int neverc_krt_hook_unregister(struct neverc_krt_hook_ref *ref);
```

توقيع المعالج:

```c
long my_hook(void *orig, void *a0, void *a1, void *a2, void *a3, void *a4, void *a5);
```

## البناء

```bash
cd examples/android-kernel-inline-hook
neverc make
```

غيّر `KERNEL` إلى `515` أو `601` أو `606` أو `612` لإصدارات نواة أخرى.

## النشر والتشغيل

```bash
neverc make run
```

أو يدوياً:

```bash
adb push nvk_hook_demo.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_hook_demo.ko'
adb shell su -c 'dmesg | grep neverc_krt_hook_demo'
```

## إلغاء التحميل

```bash
neverc make rmmod
```
