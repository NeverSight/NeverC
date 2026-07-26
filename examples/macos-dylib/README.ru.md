**Языки**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Примеры NeverC](../../docs/examples/README.ru.md)

# Пример динамической библиотеки macOS

Нативная динамическая библиотека macOS `.dylib`, кросс-скомпилированная с помощью NeverC. Оборачивает интерфейсы ядра Mach для получения информации о задачах и операций с виртуальной памятью — для исследований безопасности. Сборка из macOS, Windows или Linux — Xcode не требуется.

## Сборка

Из репозитория (цель по умолчанию: `arm64-apple-macos`):

```bash
cd examples/macos-dylib
neverc make
```

Сборка для Intel:

```bash
neverc make TARGET=x86_64-apple-macos
```

С использованием автономной версии NeverC:

```bash
neverc make NEVERC=/path/to/neverc
```

## Ручная сборка (без Make)

```bash
neverc --target=arm64-apple-macos -Wall -dynamiclib -o libneverc.dylib lib.c
```

## Возможности

- Экспортирует обёртку `nc_task_basic_info` для запросов Mach `task_info`
- Предоставляет `nc_vm_read`/`nc_vm_write` для чтения/записи виртуальной памяти Mach
- `nc_vm_alloc`/`nc_vm_dealloc` для выделения и освобождения памяти Mach VM
- Вспомогательная функция XOR-шифрования буфера и запросы PID/задачи
