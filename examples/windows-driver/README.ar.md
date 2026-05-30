**اللغات**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# مثال على برنامج تشغيل نواة Windows

برنامج تشغيل نواة WDM بسيط مبني باستخدام NeverC. يدعم التجميع المتقاطع من macOS / Linux.

NeverC هو مترجم متكامل — استدعاء واحد يتولى المعالجة المسبقة والتجميع
والتحسين (auto-LTO) والربط عبر الرابط المدمج.

## البناء

من المستودع:

```bash
cd examples/windows-driver
make
```

من إصدار NeverC مستقل:

```bash
make NEVERC=/path/to/neverc
```

الناتج هو `ExampleDriver.sys` (محسّن بـ auto-LTO).
البناء الافتراضي يتضمن `-g` للتصحيح؛ **يجب إزالة `-g` في إصدارات الإنتاج**
لإزالة رموز التصحيح وتقليل حجم الملف الثنائي (~38 كيلوبايت → ~3 كيلوبايت).

## البناء اليدوي (بدون Make)

```bash
neverc --target=x86_64-pc-windows-msvc \
  -g \
  -fms-kernel -fms-extensions -fms-compatibility \
  -D_AMD64_ -DNTDDI_VERSION=0x06010000 -D_WIN32_WINNT=0x0601 \
  -Wall -nostdlib -shared \
  -Xlinker --entry=DriverEntry \
  -Xlinker --subsystem=native \
  -Xlinker --nodefaultlib \
  -lntoskrnl -lhal \
  -o ExampleDriver.sys driver.c
```

> `-g` يضمّن معلومات تصحيح DWARF في ملف PE؛ يمكن فحصها باستخدام `llvm-dwarfdump`.
> احذف هذا الخيار في إصدارات الإنتاج لتقليل حجم الملف الثنائي.

## الوظائف

- ينشئ كائن جهاز في `\Device\ExampleDriver`
- ينشئ رابط رمزي في `\DosDevices\ExampleDriver`
- يعالج `IRP_MJ_CREATE` و `IRP_MJ_CLOSE` و `IRP_MJ_DEVICE_CONTROL`
- يطبع رسائل التحميل/الإزالة عبر `DbgPrint`

## التحميل (على جهاز اختبار Windows)

```cmd
sc create ExampleDriver type= kernel binPath= C:\path\to\ExampleDriver.sys
sc start ExampleDriver
sc stop ExampleDriver
sc delete ExampleDriver
```

قم بتفعيل التوقيع التجريبي أو استخدم شهادة توقيع الكود للإنتاج.
