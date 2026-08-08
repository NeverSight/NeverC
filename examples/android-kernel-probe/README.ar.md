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
neverc make          # debug: ‏-g (الافتراضي في أول بناء)
neverc make release  # release: ‏-O2 --strip
neverc make debug    # العودة إلى debug
```

اختر إعداد نواة آخر، مثلاً، بواسطة `neverc make KERNEL=612 release`. يحفظ
Makefile كلاً من `KERNEL` و`PROFILE`، لذلك تستخدم أوامر `make push`/`run`
اللاحقة الأثر الذي اخترته ولا تعود بصمت إلى ملف تعريف آخر.

التجريد في وضع release مدمج داخل NeverC ومقيد بسياسة آمنة لوحدات النواة. فهو
يحذف DWARF و`.comment` وأسماء الرموز الخاصة/غير المعرّفة التي لا تحتاجها
عمليات النقل، لكنه يبقي جداول الرموز/السلاسل ET_REL وعمليات النقل والواردات
والتعريفات العامة و`__versions` و`.codetag.alloc_tags` وبيانات ABI الخاصة
بالمحمّل. هذا ليس strip-all ولا تمويهاً؛ قد تبقى الأسماء اللازمة لعمليات
النقل. نفّذ التجريد قبل توقيع البايتات النهائية. لا تجرّد داخل `clean`، ولا
تستخدم `llvm-strip --strip-all` مع ملف `.ko`، ولا تحذف
`.codetag.alloc_tags` أو `__codetag_*` عشوائياً.

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

## سجل النواة (مباشر)

على الجهاز، يبث `cat /proc/kmsg` مخزن حلقة النواة في الوقت الفعلي — شبيه بـ **DbgView** على Windows. استخدمه عندما يفشل `insmod` برسالة غامضة أو تحتاج سبب الرفض الحقيقي (vermagic وmodversions وحجم القسم، إلخ).

الطرفية 1 (اتركها تعمل):

```bash
adb shell
su
cat /proc/kmsg
```

الطرفية 2:

```bash
adb shell su -c 'insmod /data/local/tests/nvk_probe.ko'
```

تظهر الأسطر الجديدة في الطرفية 1 لحظة التحميل. اضغط Ctrl+C للإيقاف.

ملاحظة: `dmesg -w` غير متوفر على بعض إصدارات Android؛ `/proc/kmsg` يحتاج root لكنه أكثر موثوقية لتصحيح تحميل الوحدات.

## إلغاء التحميل

```bash
neverc make rmmod
```

</div>
