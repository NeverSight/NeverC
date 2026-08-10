<div dir="rtl">

**اللغات**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← أمثلة NeverC](../../docs/examples/README.ar.md)

# عرض SDK الكامل لنواة Android

تكامل SDK الكامل — يُهيئ جميع أنظمة NVK الفرعية ويعرضها عبر واجهة أوامر netlink. التنفيذ المرجعي لوحدات الإنتاج. يشمل: محرك Interpose، أغلفة بيانات الاعتماد، رؤية الوحدة، التحكم في سياسة SELinux، تعداد العمليات، فحص VMA، إدخال/إخراج الملفات، كشف البيئة، والإحصائيات.

## البناء

```bash
cd examples/android-kernel-full
neverc make          # debug: ‏-g (الافتراضي في أول بناء)
neverc make release  # release: ‏-O2 --strip
neverc make debug    # العودة إلى debug
```

اختر إعداد نواة آخر، مثلاً، بواسطة `neverc make KERNEL=612 release`.
يتوسع الأمر `neverc make release` إلى `-O2 --strip`. يسجل Makefile قيمتي
`KERNEL` و`PROFILE` المختارتين في `.nvk-build-flags`، ولذلك تستخدم أوامر
`make push` و`make run` و`make` بلا هدف الأثر نفسه لاحقًا. عند غياب ملف الحالة
هذا، يستخدم `make` بناء debug افتراضيًا. يحدّث `make debug` أو `PROFILE=...`
الصريح ملف التعريف المحفوظ، ويحذف `make clean` ملف الحالة ليعود البناء التالي
إلى debug.

تكتب NeverC خمس فئات من أسماء الإصدار المستوحاة من IDA من دون استخدام بادئاتها
المحجوزة: الدوال `fn_HEX`، والتسميات التنفيذية عديمة النوع `code_HEX`، والكائنات
`obj_HEX`، والتسميات الأخرى عديمة النوع `sym_HEX`، والرموز المطلقة `abs_HEX`.
في التعريف العادي المخصص يكون `HEX` هو `analysis EA` حتميًا مشتقًا من التخطيط
النهائي لأقسام `SHF_ALLOC` (أما `abs_HEX` فيستخدم `st_value` المطلق)؛ وهو ليس
hash (تجزئة)، ولا encryption (تشفيرًا)، ولا file offset (إزاحة ملف)، ولا ELF
virtual address (عنوان ELF افتراضيًا)، ولا runtime kernel address (عنوان النواة
وقت التشغيل). ولا تخزن NeverC صيغ `sub_`/`loc_` المحجوزة أو أسماء عادية فارغة
عمدًا.

راجع [سياسة الإصدار والتجريد](../../docs/release-builds/README.ar.md) لمعرفة
الأسماء الواجب إبقاؤها كما هي، ومعنى عرض `extern` التركيبي في IDA، وحدود الأمان،
وترتيب الإنهاء والتوقيع.

## النشر والتشغيل

```bash
neverc make run
```

أو يدويًا:

```bash
adb push nvk_full.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_full.ko'
adb shell su -c 'dmesg | grep neverc_krt_full'
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
adb shell su -c 'insmod /data/local/tests/nvk_full.ko'
```

تظهر الأسطر الجديدة في الطرفية 1 لحظة التحميل. اضغط Ctrl+C للإيقاف.

ملاحظة: `dmesg -w` غير متوفر على بعض إصدارات Android؛ `/proc/kmsg` يحتاج root لكنه أكثر موثوقية لتصحيح تحميل الوحدات.

## إلغاء التحميل

```bash
neverc make rmmod
```

أو يدويًا:

```bash
adb shell su -c 'rmmod neverc_krt_full'
```

</div>
