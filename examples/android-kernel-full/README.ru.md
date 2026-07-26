**Языки**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Примеры NeverC](../../docs/examples/README.ru.md)

# Полная демо SDK ядра Android

Полная интеграция SDK — инициализирует все подсистемы NVK и предоставляет их через интерфейс команд netlink. Эталонная реализация для продакшн-модулей. Охватывает: движок interpose, обёртки учётных данных, видимость модуля, управление политикой SELinux, перечисление процессов, инспекцию VMA, файловый ввод/вывод, определение окружения и статистику.

## Сборка

```bash
cd examples/android-kernel-full
neverc make
```

Измените `KERNEL` на `515`, `601`, `606` или `612` для других версий.

## Развёртывание и запуск

```bash
neverc make run
```

Или вручную:

```bash
adb push nvk_full.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_full.ko'
adb shell su -c 'dmesg | grep nvk_full'
```

## Выгрузка

```bash
neverc make rmmod
```

Или вручную:

```bash
adb shell su -c 'rmmod nvk_full'
```
