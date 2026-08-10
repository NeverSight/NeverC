**Языки**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Примеры NeverC](../../docs/examples/README.ru.md)

# Android Kernel Hello

Минимальный модуль ядра Android NeverC (.ko). Загружает `kallsyms_lookup_name` через kprobe, выводит сообщение и корректно завершается. Простейшая сквозная проверка: компиляция → линковка → insmod.

## Сборка

```bash
cd examples/android-kernel-hello
neverc make          # debug: -g (по умолчанию при первой сборке)
neverc make release  # release: -O2 --strip
neverc make debug    # вернуться к debug
```

Другой профиль ядра можно выбрать, например, командой
`neverc make KERNEL=612 release`. `neverc make release` выбирает
`-O2 --strip`. Makefile записывает выбранные значения `KERNEL` и `PROFILE` в
`.nvk-build-flags`, поэтому последующие `make push`, `make run` и `make` без
цели используют тот же артефакт. Без этого файла состояния `make` по умолчанию
использует debug. `make debug` или явный `PROFILE=...` заменяет сохранённый
профиль; `make clean` удаляет файл, и следующая сборка снова использует debug.

NeverC записывает пять классов вдохновлённых IDA, но не зарезервированных
релизных имён: функции `fn_HEX`, исполняемые бестиповые метки `code_HEX`, объекты
`obj_HEX`, прочие бестиповые метки `sym_HEX` и абсолютные символы `abs_HEX`.
Для обычного размещённого определения `HEX` — детерминированный `analysis EA`,
вычисленный из окончательной раскладки секций `SHF_ALLOC` (`abs_HEX` вместо
этого использует абсолютный `st_value`); это не hash (хеш), encryption
(шифрование), file offset (смещение в файле), ELF virtual address (виртуальный
адрес ELF) или runtime kernel address (адрес ядра во время выполнения). NeverC
не сохраняет зарезервированные формы `sub_`/`loc_` и намеренно пустые обычные
имена.

Точно сохраняемые имена, синтетическое представление `extern` в IDA, границы
безопасности и порядок финализации и подписания описывает
[политика release/strip](../../docs/release-builds/README.ru.md).

## Развёртывание и запуск

```bash
neverc make run
```

Или вручную:

```bash
adb push nvk_hello.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_hello.ko'
adb shell su -c 'dmesg | grep neverc_krt_hello'
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
adb shell su -c 'insmod /data/local/tests/nvk_hello.ko'
```

Новые строки появятся в терминале 1 в момент загрузки. Ctrl+C — остановка.

Примечание: на некоторых сборках Android нет `dmesg -w`; для `/proc/kmsg` нужен root, но для отладки загрузки модулей это надёжнее.

## Выгрузка

```bash
neverc make rmmod
```

Или вручную:

```bash
adb shell su -c 'rmmod neverc_krt_hello'
```
