**Языки**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Хук системного вызова ядра Android

Замена таблицы системных вызовов для `openat`. По умолчанию: замена записи таблицы. С `-DNVK_SYSCALL_INLINE_HOOK`: патчит пролог функции-обработчика. Демонстрирует `nvk_syscall_replace`/`nvk_syscall_restore` и определения номеров arm64 syscall.

## Сборка

```bash
cd examples/android-kernel-syscall-hook
neverc make
```

Измените `KERNEL` на `515`, `601`, `606` или `612` для других версий.

## Развёртывание и запуск

```bash
neverc make run
```

Или вручную:

```bash
adb push nvk_syscall_hook.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_syscall_hook.ko'
adb shell su -c 'dmesg | grep nvk_syscall_hook'
```

## Выгрузка

```bash
neverc make rmmod
```

Или вручную:

```bash
adb shell su -c 'rmmod nvk_syscall_hook'
```
