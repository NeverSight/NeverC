**Языки**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Примеры NeverC](../../docs/examples/README.ru.md)

# Пример драйвера ядра Windows

Минимальный драйвер ядра WDM, собранный с помощью NeverC. По умолчанию нацелен
на **x64**, также может быть собран для ARM64. Кросс-компиляция с macOS / Linux.

NeverC — это универсальный компилятор: один вызов выполняет препроцессинг,
компиляцию, оптимизацию (auto-LTO) и линковку через встроенный линкер.

## Сборка

Из репозитория:

```bash
cd examples/windows-driver
neverc make
```

Это создаёт `ExampleDriver-x64.sys`. Чтобы собрать для ARM64 или для обеих архитектур:

```bash
neverc make ARCH=arm64
neverc make all-arch
```

Из автономной сборки NeverC:

```bash
neverc make NEVERC=/path/to/neverc
```

Результат — `ExampleDriver-<арх>.sys` (оптимизирован auto-LTO).
Сборка по умолчанию включает `-g` для отладки; **в релизных сборках следует убрать
`-g`**, чтобы удалить отладочные символы и уменьшить размер бинарного файла
(~38 КБ → ~3 КБ).

## Ручная сборка (без Make)

```bash
neverc --target=x86_64-pc-windows-msvc \
  -g \
  -fms-kernel \
  -Wall -nostdlib -shared \
  -Xlinker --entry=DriverEntry \
  -Xlinker --subsystem=native \
  -Xlinker --nodefaultlib \
  -lntoskrnl -lhal \
  -o ExampleDriver-x64.sys driver.c
```

Для ARM64 достаточно заменить цель на `aarch64-pc-windows-msvc`; остальное не
меняется. `-fms-kernel` подбирает заголовки и библиотеки импорта WDK,
соответствующие цели, и определяет ожидаемые WDK макросы архитектуры, так что
передавать их вручную не нужно.

> `-g` встраивает отладочную информацию DWARF в PE; проверяйте с помощью
> `llvm-dwarfdump`. В релизных сборках опускайте эту опцию для уменьшения
> размера бинарного файла.

## Функциональность

- Создаёт объект устройства в `\Device\ExampleDriver`
- Создаёт символическую ссылку в `\DosDevices\ExampleDriver`
- Обрабатывает `IRP_MJ_CREATE`, `IRP_MJ_CLOSE`, `IRP_MJ_DEVICE_CONTROL`
- Выводит сообщения о загрузке/выгрузке через `DbgPrint`

## Загрузка (на тестовой машине Windows)

```cmd
sc create ExampleDriver type= kernel binPath= C:\path\to\ExampleDriver-x64.sys
sc start ExampleDriver
sc stop ExampleDriver
sc delete ExampleDriver
```

Включите тестовую подпись или используйте сертификат подписи кода для продакшена.
