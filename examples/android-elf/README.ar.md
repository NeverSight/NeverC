<div dir="rtl">

**اللغات**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← أمثلة NeverC](../../docs/examples/README.ar.md)

# مثال Android ELF

ملف تنفيذي ELF أصلي ARM64 مُترجم تبادلياً لنظام Android باستخدام NeverC. مُصمم للتشغيل مباشرة على أجهزة Android المروّتة عبر `adb shell`. يمكن البناء من macOS أو Windows أو Linux — بدون الحاجة إلى Android NDK أو CMake.

يتضمن NeverC نظام sysroot خاص بـ Android (NDK r26c, API 21+) في `runtime/android/`، لذا فإن استدعاءً واحداً يتولى المعالجة المسبقة والترجمة والتحسين (LTO التلقائي) والربط.

## البناء

من المستودع:

```bash
cd examples/android-elf
neverc make          # debug: ‏-g (الافتراضي في أول بناء)
neverc make release  # release: ‏-O2 --strip
neverc make debug    # العودة إلى debug
```

يحفظ Makefile قيمة `PROFILE`، لذا تُبقي أوامر `neverc make` اللاحقة
نفس اختيار debug/release. يستخدم الإصدار `--strip` المدمج في NeverC:
يزيل بيانات التصحيح وأسماء الرموز الساكنة غير اللازمة ويُبقي أسماء
ABI الديناميكية/المحمّل المطلوبة. انظر
[ملفات الإصدار](../../docs/release-builds/README.ar.md).

باستخدام إصدار مستقل من NeverC:

```bash
neverc make NEVERC=/path/to/neverc
```

## البناء اليدوي (بدون Make)

```bash
neverc --target=aarch64-linux-android -Wall -fPIE -lm -ldl -llog -o android-elf main.c
```

## النشر والتشغيل

نقل إلى الجهاز والتشغيل عبر adb:

```bash
neverc make run
```

أو يدوياً:

```bash
adb push android-elf /data/local/tests/
adb shell chmod 755 /data/local/tests/android-elf
adb shell /data/local/tests/android-elf
```

## الميزات

- يعرض معلومات الجهاز (`uname`) وإصدار النواة
- يتحقق من حالة الروت/الصلاحيات (`uid`/`euid`، مسارات `su`)
- يحمّل `liblog.so` ديناميكياً ويستدعي `__android_log_print`
- يقرأ `/proc/self/maps` لعرض خريطة الذاكرة
- يوضح `dlopen`/`dlsym` و`readlink` و`fopen` على Android

</div>
