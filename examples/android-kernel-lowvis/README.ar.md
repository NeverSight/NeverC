**اللغات**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# إدارة رؤية وحدة نواة Android

عرض إدارة رؤية الوحدة. الأعلام: بدون=رؤية قائمة أساسية، `-DNVK_LOWVIS_FILTER`=مرشّح رؤية كامل (قائمة+sysfs+proc)، `-DNVK_LOWVIS_FILTER_FULL`=موسّع (dmesg+PID+تركيب+maps)، `-DNVK_LOWVIS_CRED`=عرض أغلفة بيانات الاعتماد (`struct cred`)، `-DNVK_LOWVIS_SELINUX`=عرض حالة إنفاذ SELinux (permissive).

## البناء

```bash
cd examples/android-kernel-lowvis
neverc make
```

غيّر `KERNEL` إلى `515` أو `601` أو `606` أو `612` لإصدارات أخرى.

## النشر والتشغيل

```bash
neverc make run
```

أو يدويًا:

```bash
adb push nvk_lowvis.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_lowvis.ko'
adb shell su -c 'dmesg | grep nvk_lowvis'
```

## إلغاء التحميل

```bash
neverc make rmmod
```

أو يدويًا:

```bash
adb shell su -c 'rmmod nvk_lowvis'
```
