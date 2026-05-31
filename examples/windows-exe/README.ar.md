**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# مثال Windows Ring3 EXE

ملف تنفيذي Windows في وضع المستخدم مُترجم تبادلياً باستخدام NeverC.

## البناء

```bash
cd examples/windows-exe
make
```

## البناء اليدوي

```bash
neverc --target=x86_64-pc-windows-msvc -Wall -Xlinker --subsystem=console -lkernel32 -luser32 -lmsvcrt -o example.exe main.c
```

## الميزات

- استعلام معلومات النظام عبر `GetSystemInfo`
- تعداد العمليات باستخدام `CreateToolhelp32Snapshot`
- عرض `VirtualAlloc`/`VirtualQuery`/`VirtualFree`

