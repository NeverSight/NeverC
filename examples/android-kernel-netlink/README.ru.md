**Языки**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Примеры NeverC](../../docs/examples/README.ru.md)

# Netlink ядра Android

Двунаправленный IPC-канал netlink. Создаёт netlink-сокет для связи пользователь↔ядро. Поддерживает PING (возвращает PONG), VERSION (строка версии ядра) и ECHO. Демонстрирует `nvk_nl_open`, `nvk_nl_reply` и паттерн dispatch-callback.

## Сборка

```bash
cd examples/android-kernel-netlink
neverc make
```

Измените `KERNEL` на `515`, `601`, `606` или `612` для других версий.

## Развёртывание и запуск

```bash
neverc make run
```

Или вручную:

```bash
adb push nvk_netlink.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_netlink.ko'
adb shell su -c 'dmesg | grep neverc_krt_netlink'
```

## Журнал ядра (в реальном времени)

На устройстве `cat /proc/kmsg` выводит ring buffer ядра в реальном времени — аналог **DbgView** в Windows. Используйте, когда `insmod` падает с неясной ошибкой или нужна точная причина отказа (vermagic, modversions, размер section и т. д.).

Терминал 1 (оставить работать):

```bash
adb shell
su
cat /proc/kmsg
```

Терминал 2:

```bash
adb shell su -c 'insmod /data/local/tests/nvk_netlink.ko'
```

Новые строки появятся в терминале 1 в момент загрузки. Ctrl+C — остановка.

Примечание: на некоторых сборках Android нет `dmesg -w`; для `/proc/kmsg` нужен root, но для отладки загрузки модулей это надёжнее.

## Выгрузка

```bash
neverc make rmmod
```

Или вручную:

```bash
adb shell su -c 'rmmod neverc_krt_netlink'
```
