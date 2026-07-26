**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Примеры NeverC](../../docs/examples/README.ru.md)

# Пример общей библиотеки Android

Нативная ARM64 `.so` разделяемая библиотека, кросс-компилированная для Android с помощью NeverC. Сборка с macOS, Windows или Linux.

## Сборка

```bash
cd examples/android-so
neverc make
```

## Ручная сборка

```bash
neverc --target=aarch64-linux-android -Wall -shared -fPIC -ldl -o libneverc.so lib.c
```

## Функциональность

- Вспомогательные функции для исследования безопасности игр: запрос PID, чтение `/proc/self/maps`, выделение RWX-памяти, XOR-шифрование буфера
- Динамическая загрузка `liblog.so` через `dlopen`
- Демо выделения исполняемой памяти с `mmap` + `PROT_EXEC`

