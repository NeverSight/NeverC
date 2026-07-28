<div dir="rtl">

**اللغات**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← أمثلة NeverC](../../docs/examples/README.ar.md)

# مثال على برنامج تشغيل نواة Windows

برنامج تشغيل نواة WDM بسيط مبني باستخدام NeverC. يستهدف **x64** افتراضيًا،
ويمكن بناؤه أيضًا لـ ARM64. يدعم التجميع المتقاطع من macOS / Linux.

NeverC هو مترجم متكامل — استدعاء واحد يتولى المعالجة المسبقة والتجميع
والتحسين (auto-LTO) والربط عبر الرابط المدمج.

## البناء

من المستودع:

```bash
cd examples/windows-driver
neverc make
```

ينتج عن ذلك `ExampleDriver-x64.sys`. للبناء لـ ARM64 أو لكليهما:

```bash
neverc make ARCH=arm64
neverc make all-arch
```

من إصدار NeverC مستقل:

```bash
neverc make NEVERC=/path/to/neverc
```

الناتج هو `ExampleDriver-<المعمارية>.sys` (محسّن بـ auto-LTO).
البناء الافتراضي يتضمن `-g` للتصحيح؛ **يجب إزالة `-g` في إصدارات الإنتاج**
لإزالة رموز التصحيح وتقليل حجم الملف الثنائي (~38 كيلوبايت → ~3 كيلوبايت).

## البناء اليدوي (بدون Make)

```bash
neverc --target=x86_64-pc-windows-msvc \
  -g \
  -fms-kernel \
  -Wall -nostdlib -shared \
  -Xlinker --entry=DriverEntry \
  -Xlinker --subsystem=native \
  -Xlinker --nodefaultlib \
  -lntoskrnl -lhal \
  -o ExampleDriver-x64.sys driver.c
```

بالنسبة إلى ARM64، يكفي تغيير الهدف إلى `aarch64-pc-windows-msvc`؛ ولا يتغير
أي شيء آخر. يقوم `-fms-kernel` باختيار ترويسات WDK ومكتبات الاستيراد المطابقة
للهدف، ويعرّف وحدات ماكرو المعمارية التي يتوقعها WDK، لذا لا حاجة لتمريرها يدويًا.

> `-g` يضمّن معلومات تصحيح DWARF في ملف PE؛ يمكن فحصها باستخدام `llvm-dwarfdump`.
> احذف هذا الخيار في إصدارات الإنتاج لتقليل حجم الملف الثنائي.

## التوقيع التجريبي

يرفض Windows تحميل برنامج تشغيل نواة غير موقّع. يقوم `-ftest-sign` بإرفاق توقيع
Authenticode حتى يجتاز الملف هذا الفحص على جهاز اختبار:

```bash
neverc make TESTSIGN=1
neverc make ARCH=arm64 TESTSIGN=1
```

أو أضف `-ftest-sign` عند الاستدعاء اليدوي. لا يُقبل هذا الخيار إلا مع
`-fms-kernel`، لأن التوقيع التجريبي لا معنى له لملف ثنائي في وضع المستخدم.

هوية التوقيع مضمّنة داخل المترجم — شهادة موقّعة ذاتيًا مفتاحها الخاص علني بحكم
التصميم. وهي لا تمنح أي أصالة؛ بل تكتفي باجتياز فحص سلامة الكود على جهاز فتحته
عمدًا. هيّئ ذلك الجهاز مرة واحدة بصلاحيات المسؤول:

```cmd
bcdedit /set testsigning on
certutil -addstore Root neverc-test-signing.cer
certutil -addstore TrustedPublisher neverc-test-signing.cer
```

ثم أعد التشغيل. توجد الشهادة في `utils/neverc-test-signing.cer`.

**لا تستخدمها أبدًا لأي شيء يغادر جهاز الاختبار.** في الإنتاج، وقّع بشهادة توقيع
كود حقيقية (وبالنسبة لـ Windows 10 1607 وما بعده، توقيع إثبات من Microsoft
Hardware Dev Center أيضًا).

## الوظائف

- ينشئ كائن جهاز في `\Device\ExampleDriver`
- ينشئ رابط رمزي في `\DosDevices\ExampleDriver`
- يعالج `IRP_MJ_CREATE` و `IRP_MJ_CLOSE` و `IRP_MJ_DEVICE_CONTROL`
- يطبع رسائل التحميل/الإزالة عبر `DbgPrint`

## التحميل (على جهاز اختبار Windows)

```cmd
sc create ExampleDriver type= kernel binPath= C:\path\to\ExampleDriver-x64.sys
sc start ExampleDriver
sc stop ExampleDriver
sc delete ExampleDriver
```

قم بتفعيل التوقيع التجريبي أو استخدم شهادة توقيع الكود للإنتاج.

</div>
