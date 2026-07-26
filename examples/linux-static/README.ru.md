**Языки**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Примеры NeverC](../../docs/examples/README.ru.md)

# Пример полностью статического Linux-бинарника

Автономный статически связанный исполняемый файл Linux. Нулевые зависимости времени выполнения.

NeverC включает Linux sysroot (Ubuntu 22.04, glibc 2.35) в `runtime/linux/`.

## Сборка

```bash
cd examples/linux-static
neverc make
```

AArch64:

```bash
neverc make TARGET=aarch64-linux-gnu
```

## Ручная сборка

```bash
neverc --target=x86_64-linux-gnu -Wall -static -lm -o static-demo main.c
```

## Запуск

```bash
chmod +x static-demo
./static-demo
```

## Возможности

- Информация о системе
- Математические функции: `sqrt`, `sin`, `pow`, `log`
- Строковые операции, управление динамической памятью
