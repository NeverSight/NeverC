**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android Kernel Syscall Hook

Syscall-Tabellen-Ersetzung auf `openat`. Standard: Tabelleneintrag-Tausch. Mit `-DNVK_SYSCALL_INLINE_HOOK`: patcht den Handler-Funktionsprolog. Demonstriert `nvk_syscall_replace`/`nvk_syscall_restore` und arm64 Syscall-Nummern.

## Kompilierung

```bash
cd examples/android-kernel-syscall-hook
neverc make
```

Ändern Sie `KERNEL` auf `515`, `601`, `606` oder `612` für andere Versionen.

## Bereitstellung und Ausführung

```bash
neverc make run
```

Oder manuell:

```bash
adb push nvk_syscall_hook.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_syscall_hook.ko'
adb shell su -c 'dmesg | grep nvk_syscall_hook'
```

## Entladen

```bash
neverc make rmmod
```

Oder manuell:

```bash
adb shell su -c 'rmmod nvk_syscall_hook'
```
