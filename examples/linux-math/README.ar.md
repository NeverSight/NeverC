**اللغات**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# مثال Linux الرياضيات + zlib

دوال المكتبة الرياضية وضغط zlib. يستخدم `-lm` و`-lz`.

يتضمن NeverC sysroot لنظام Linux (Ubuntu 22.04، glibc 2.35) في `runtime/linux/`.

## البناء

```bash
cd examples/linux-math
make
```

AArch64:

```bash
make TARGET=aarch64-linux-gnu
```

## البناء اليدوي

```bash
neverc --target=x86_64-linux-gnu -O2 -Wall -lm -lz -o math-demo main.c
```

## التشغيل

```bash
chmod +x math-demo
./math-demo
```

## الميزات

- حساب المثلثات: sin/cos/tan
- دوال خاصة: `exp`، `tgamma`، `erf`
- ضغط/فك ضغط zlib، CRC32
