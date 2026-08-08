<div dir="rtl">

**اللغات**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← أمثلة NeverC](../../docs/examples/README.ar.md)

# مثال Linux Hello World

برنامج C بسيط يتم ترجمته تبادلياً إلى Linux ELF باستخدام NeverC. يمكن البناء من macOS أو Windows أو Linux — دون الحاجة إلى سلسلة أدوات النظام المستهدف.

يتضمن NeverC sysroot لنظام Linux (Ubuntu 22.04، glibc 2.35) في `runtime/linux/`، بحيث يتعامل استدعاء واحد مع المعالجة المسبقة والترجمة والتحسين (auto-LTO) والربط عبر الرابط المدمج.

## البناء

من المستودع (الهدف الافتراضي: `x86_64-linux-gnu`):

```bash
cd examples/linux-hello
neverc make          # debug: ‏-g (الافتراضي في أول بناء)
neverc make release  # release: ‏-O2 --strip
neverc make debug    # العودة إلى debug
```

يحفظ Makefile قيمة `PROFILE`، لذا تُبقي أوامر `neverc make` اللاحقة
نفس اختيار debug/release. يستخدم الإصدار `--strip` المدمج في NeverC:
يزيل بيانات التصحيح وأسماء الرموز الساكنة غير اللازمة ويُبقي أسماء
ABI الديناميكية/المحمّل المطلوبة. انظر
[ملفات الإصدار](../../docs/release-builds/README.ar.md).


البناء لـ AArch64:

```bash
neverc make TARGET=aarch64-linux-gnu
```

باستخدام إصدار مستقل من NeverC:

```bash
neverc make NEVERC=/path/to/neverc
```

## البناء اليدوي (بدون Make)

```bash
neverc --target=x86_64-linux-gnu -Wall -o hello main.c
```

## التشغيل

انسخ `hello` إلى جهاز Linux (أو حاوية Docker) ونفّذ:

```bash
chmod +x hello
./hello
```

## الميزات

- يطبع رسالة ترحيب مع وسائط سطر الأوامر
- يوضح `printf` و`strncpy` و`strlen` و`atoi` من libc المضمنة
- تحويل XOR لسلسلة نصية للتحقق من العمليات الأساسية على الأعداد الصحيحة/الأحرف

</div>
