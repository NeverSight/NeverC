**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# مثال Windows Ring3 DLL

DLL Windows في وضع المستخدم مُترجمة تبادلياً باستخدام NeverC.

## البناء

```bash
cd examples/windows-dll
make
```

## البناء اليدوي

```bash
neverc --target=x86_64-pc-windows-msvc -Wall -shared -Xlinker --entry=DllMain -Xlinker --subsystem=windows -lkernel32 -luser32 -o example.dll dllmain.c
```

## الميزات

- تصدير أغلفة وصول الذاكرة عبر العمليات
- تعداد العمليات/الوحدات
- مساعد تشفير XOR

