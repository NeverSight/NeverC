**Языки**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Примеры NeverC](../../docs/examples/README.ru.md)

# Управление видимостью модуля ядра Android

Демо управления видимостью модуля. Флаги: нет=базовая видимость списка, `-DNVK_LOWVIS_FILTER`=полный фильтр видимости (список+sysfs+proc), `-DNVK_LOWVIS_FILTER_FULL`=расширенное (dmesg+PID+mount+maps), `-DNVK_LOWVIS_CRED`=демо обёрток учётных данных (`struct cred`), `-DNVK_LOWVIS_SELINUX`=демо состояния enforcement SELinux (permissive).

## Сборка

```bash
cd examples/android-kernel-lowvis
neverc make
```

Измените `KERNEL` на `515`, `601`, `606` или `612` для других версий.

## Развёртывание и запуск

```bash
neverc make run
```

Или вручную:

```bash
adb push nvk_lowvis.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_lowvis.ko'
adb shell su -c 'dmesg | grep neverc_krt_lowvis'
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
adb shell su -c 'insmod /data/local/tests/nvk_lowvis.ko'
```

Новые строки появятся в терминале 1 в момент загрузки. Ctrl+C — остановка.

Примечание: на некоторых сборках Android нет `dmesg -w`; для `/proc/kmsg` нужен root, но для отладки загрузки модулей это надёжнее.

## Выгрузка

```bash
neverc make rmmod
```

Или вручную:

```bash
adb shell su -c 'rmmod neverc_krt_lowvis'
```
