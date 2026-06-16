**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android Kernel Inline Hook

Inline Hook auf `do_faccessat`. Standard: einfache Ersetzung mit Trampoline. Mit `-DNVK_CONTEXT_HOOK`: Kontext-Hook mit vollständigem `nvk_reg_ctx` Registerzustand. Demonstriert BTI/PAC-sicheres Patching, PC-relative Relokation und D-Cache→I-Cache-kohärentes Trampoline.

## Kompilierung

```bash
cd examples/android-kernel-inline-hook
neverc make
```

Ändern Sie `KERNEL` auf `515`, `601`, `606` oder `612` für andere Versionen.

## Bereitstellung und Ausführung

```bash
neverc make run
```

Oder manuell:

```bash
adb push nvk_inline_hook.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_inline_hook.ko'
adb shell su -c 'dmesg | grep nvk_inline_hook'
```

## Entladen

```bash
neverc make rmmod
```

Oder manuell:

```bash
adb shell su -c 'rmmod nvk_inline_hook'
```
