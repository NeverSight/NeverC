**Языки**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android Kernel Multi-File Module

Многофайловый модуль ядра NeverC. Ключевые моменты:

- **Однократный bootstrap**: `NEVERC_KRT_BOOTSTRAP()` вызывается только раз в `module_init`
- **Общее состояние**: компилятор повышает все состояние `neverc_krt_*` до `weak_odr` линковки, все `.c` файлы разделяют один резолвер, кеш и состояние подсистем
- **Разделенная архитектура**: `main.c` (init/exit), `hooks.c` (логика хуков), `utils.c` (хелперы)

## Сборка

```bash
cd examples/android-kernel-multifile
neverc make
```

Измените `KERNEL` на `515`, `601`, `606` или `612` для других версий ядра.

## Развертывание и запуск

```bash
neverc make run
```

Или вручную:

```bash
adb push nvk_multi.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_multi.ko'
adb shell su -c 'dmesg | grep neverc_krt_multi'
```

## Выгрузка

```bash
neverc make rmmod
```
