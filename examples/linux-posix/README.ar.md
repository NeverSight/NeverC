<div dir="rtl">

**اللغات**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← أمثلة NeverC](../../docs/examples/README.ar.md)

# مثال Linux POSIX API

برمجة أنظمة POSIX مترجمة تبادلياً إلى Linux باستخدام NeverC: pthreads وmmap وpipe ومعالجة الإشارات.

يتضمن NeverC sysroot لنظام Linux (Ubuntu 22.04، glibc 2.35) في `runtime/linux/`.

## البناء

```bash
cd examples/linux-posix
neverc make          # debug: ‏-g (الافتراضي في أول بناء)
neverc make release  # release: ‏-O2 --strip
neverc make debug    # العودة إلى debug
```

يحفظ Makefile قيمة `PROFILE`، لذا تُبقي أوامر `neverc make` اللاحقة
نفس اختيار debug/release. يستخدم الإصدار `--strip` المدمج في NeverC:
يزيل بيانات التصحيح وأسماء الرموز الساكنة غير اللازمة ويُبقي أسماء
ABI الديناميكية/المحمّل المطلوبة. انظر
[ملفات الإصدار](../../docs/release-builds/README.ar.md).


AArch64:

```bash
neverc make TARGET=aarch64-linux-gnu
```

## البناء اليدوي

```bash
neverc --target=x86_64-linux-gnu -Wall -lpthread -o posix-demo main.c
```

## التشغيل

```bash
chmod +x posix-demo
./posix-demo
```

## الميزات

- **pthreads**: إنشاء 4 خيوط عمل
- **mmap**: تخصيص صفحة ذاكرة مجهولة
- **pipe**: إرسال رسالة عبر أنبوب Unix
- **signals**: التحقق من معالج `SIGUSR1`

</div>
