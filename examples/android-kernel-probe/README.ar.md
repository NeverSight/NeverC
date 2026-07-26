<div dir="rtl">

**اللغات**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← أمثلة NeverC](../../docs/examples/README.ar.md)

# Android Kernel Probe

ربط تعليمة عشوائية داخل `do_faccessat` (ليس نقطة الدخول) باستخدام `neverc_krt_probe_register`. يوضح:

- **ربط بعنوان عشوائي**: يمكن ربط أي تعليمة، وليس فقط مداخل الدوال
- **سياق السجلات الكامل**: قراءة/كتابة جميع GPR عبر `neverc_krt_reg_ctx`
- **التسلسل التلقائي**: عدة معالجات على نفس العنوان، تُنفَّذ حسب الأولوية
- **التحكم بالتدفق**: `NEVERC_KRT_CTX_SKIP` للإلغاء، `NEVERC_KRT_CTX_REDIRECT` لإعادة التوجيه

## API

```c
int neverc_krt_probe_register(void *addr, neverc_krt_ctx_handler_t handler,
                              int priority, struct neverc_krt_probe_ref *ref);
int neverc_krt_probe_unregister(struct neverc_krt_probe_ref *ref);
```

توقيع المعالج:

```c
void my_probe(neverc_krt_reg_ctx *ctx);
```

## البناء

```bash
cd examples/android-kernel-probe
neverc make
```

غيّر `KERNEL` إلى `515` أو `601` أو `606` أو `612` لإصدارات نواة أخرى.

## النشر والتشغيل

```bash
neverc make run
```

أو يدوياً:

```bash
adb push nvk_probe.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_probe.ko'
adb shell su -c 'dmesg | grep neverc_krt_probe_demo'
```

## إلغاء التحميل

```bash
neverc make rmmod
```

</div>
