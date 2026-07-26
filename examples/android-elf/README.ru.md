**Языки**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Примеры NeverC](../../docs/examples/README.ru.md)

# Пример Android ELF

Нативный ARM64 ELF-бинарник, кросс-компилированный для Android с помощью NeverC. Предназначен для прямого запуска на рутированных Android-устройствах через `adb shell`. Сборка возможна с macOS, Windows или Linux — без Android NDK или CMake.

NeverC включает Android sysroot (NDK r26c, API 21+) в `runtime/android/`, поэтому один вызов выполняет препроцессинг, компиляцию, оптимизацию (авто-LTO) и линковку.

## Сборка

Из репозитория:

```bash
cd examples/android-elf
neverc make
```

С автономной версией NeverC:

```bash
neverc make NEVERC=/path/to/neverc
```

## Ручная сборка (без Make)

```bash
neverc --target=aarch64-linux-android -Wall -fPIE -lm -ldl -llog -o android-elf main.c
```

## Развёртывание и запуск

Отправить на устройство и запустить через adb:

```bash
neverc make run
```

Или вручную:

```bash
adb push android-elf /data/local/tests/
adb shell chmod 755 /data/local/tests/android-elf
adb shell /data/local/tests/android-elf
```

## Функциональность

- Выводит информацию об устройстве (`uname`) и версию ядра
- Проверяет root-статус/привилегии (`uid`/`euid`, пути `su`)
- Динамически загружает `liblog.so` и вызывает `__android_log_print`
- Читает `/proc/self/maps` для отображения карты памяти
- Демонстрирует `dlopen`/`dlsym`, `readlink`, `fopen` на Android
