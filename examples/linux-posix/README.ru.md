**Языки**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Пример Linux POSIX API

Системное программирование POSIX, кросс-компилированное в Linux с помощью NeverC: pthreads, mmap, pipe и обработка сигналов.

NeverC включает Linux sysroot (Ubuntu 22.04, glibc 2.35) в `runtime/linux/`.

## Сборка

```bash
cd examples/linux-posix
make
```

AArch64:

```bash
make TARGET=aarch64-linux-gnu
```

## Ручная сборка

```bash
neverc --target=x86_64-linux-gnu -Wall -lpthread -o posix-demo main.c
```

## Запуск

```bash
chmod +x posix-demo
./posix-demo
```

## Возможности

- **pthreads**: создание 4 рабочих потоков
- **mmap**: выделение анонимной страницы памяти
- **pipe**: отправка сообщения через Unix pipe
- **signals**: проверка обработчика `SIGUSR1`
