**اللغات**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# مثال Linux مقبس الشبكة

عرض TCP عميل/خادم مترجم تبادلياً باستخدام NeverC.

يتضمن NeverC sysroot لنظام Linux (Ubuntu 22.04، glibc 2.35) في `runtime/linux/`.

## البناء

```bash
cd examples/linux-network
neverc make
```

AArch64:

```bash
neverc make TARGET=aarch64-linux-gnu
```

## البناء اليدوي

```bash
neverc --target=x86_64-linux-gnu -Wall -o network-demo main.c
```

## التشغيل

```bash
chmod +x network-demo
./network-demo
```

## الميزات

- خادم TCP (127.0.0.1)
- اتصال العميل
- إرسال 3 رسائل
- عرض `socket`، `bind`، `listen`، `accept`
