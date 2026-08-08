**Языки**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Примеры NeverC](../../docs/examples/README.ru.md)

# Пример Linux Hello World

Минимальная программа на C, кросс-компилированная в Linux ELF с помощью NeverC. Сборка с macOS, Windows или Linux — без необходимости в инструментарии целевой системы.

NeverC включает Linux sysroot (Ubuntu 22.04, glibc 2.35) в `runtime/linux/`, поэтому один вызов выполняет препроцессинг, компиляцию, оптимизацию (auto-LTO) и линковку через встроенный линкер.

## Сборка

Из репозитория (цель по умолчанию: `x86_64-linux-gnu`):

```bash
cd examples/linux-hello
neverc make          # debug: -g (по умолчанию при первой сборке)
neverc make release  # release: -O2 --strip
neverc make debug    # вернуться к debug
```

Makefile сохраняет `PROFILE`, поэтому последующие `neverc make`
оставляют тот же выбор debug/release. Release использует встроенный
`--strip` NeverC: удаляет отладочные метаданные и ненужные статические
имена символов, сохраняя нужные имена загрузчика/динамического ABI.
См. [Релизные сборки](../../docs/release-builds/README.ru.md).


Сборка для AArch64:

```bash
neverc make TARGET=aarch64-linux-gnu
```

С использованием автономного выпуска NeverC:

```bash
neverc make NEVERC=/path/to/neverc
```

## Ручная сборка (без Make)

```bash
neverc --target=x86_64-linux-gnu -Wall -o hello main.c
```

## Запуск

Скопируйте `hello` на Linux-машину (или Docker-контейнер) и запустите:

```bash
chmod +x hello
./hello
```

## Возможности

- Выводит приветствие с аргументами командной строки
- Демонстрирует `printf`, `strncpy`, `strlen`, `atoi` из встроенной libc
- XOR-преобразование строки для проверки базовых целочисленных/символьных операций
