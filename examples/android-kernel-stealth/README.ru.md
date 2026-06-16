**Языки**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Стелс-модуль ядра Android

Демо сокрытия модуля. Флаги: нет=базовое скрытие из списка, `-DNVK_STEALTH_HIDE`=полное (список+sysfs+proc), `-DNVK_STEALTH_FULL_HIDE`=расширенное (dmesg+PID+mount+maps), `-DNVK_STEALTH_ROOT`=дать root, `-DNVK_STEALTH_SELINUX`=permissive режим.

## Сборка

```bash
cd examples/android-kernel-stealth
neverc make
```

Измените `KERNEL` на `515`, `601`, `606` или `612` для других версий.

## Развёртывание и запуск

```bash
neverc make run
```

Или вручную:

```bash
adb push nvk_stealth.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_stealth.ko'
adb shell su -c 'dmesg | grep nvk_stealth'
```

## Выгрузка

```bash
neverc make rmmod
```

Или вручную:

```bash
adb shell su -c 'rmmod nvk_stealth'
```
