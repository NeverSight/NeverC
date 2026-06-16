**اللغات**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# وحدة التخفي لنواة Android

عرض إخفاء الوحدة. الأعلام: بدون=إخفاء قائمة أساسي، `-DNVK_STEALTH_HIDE`=إخفاء كامل (قائمة+sysfs+proc)، `-DNVK_STEALTH_FULL_HIDE`=موسّع (dmesg+PID+تركيب+maps)، `-DNVK_STEALTH_ROOT`=منح root، `-DNVK_STEALTH_SELINUX`=وضع متساهل.

## البناء

```bash
cd examples/android-kernel-stealth
neverc make
```

غيّر `KERNEL` إلى `515` أو `601` أو `606` أو `612` لإصدارات أخرى.

## النشر والتشغيل

```bash
neverc make run
```

أو يدويًا:

```bash
adb push nvk_stealth.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_stealth.ko'
adb shell su -c 'dmesg | grep nvk_stealth'
```

## إلغاء التحميل

```bash
neverc make rmmod
```

أو يدويًا:

```bash
adb shell su -c 'rmmod nvk_stealth'
```
