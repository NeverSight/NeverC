**Языки**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Примеры NeverC](../../docs/examples/README.ru.md)

# Android Kernel Function Interpose

Хук `do_faccessat` в точке входа с помощью `neverc_krt_interpose_register`. Демонстрирует:

- **Автоматическая цепочка**: несколько обработчиков на одной цели, выполняемых по приоритету
- **Вызов оригинала**: обработчик получает указатель `orig` для вызова исходной функции
- **Управление приоритетом**: меньшее значение = выполнение раньше; отрицательные значения для запуска перед другими хуками
- **Сосуществование**: работает даже если цель уже перехвачена другим модулем

## API

```c
int neverc_krt_interpose_register(void *target, void *handler, int priority,
                             void **orig, struct neverc_krt_interpose_ref *ref);
int neverc_krt_interpose_unregister(struct neverc_krt_interpose_ref *ref);
```

Сигнатура обработчика:

```c
long my_interpose(void *orig, void *a0, void *a1, void *a2, void *a3, void *a4, void *a5);
```

## Сборка

```bash
cd examples/android-kernel-inline-interpose
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
adb push nvk_interpose_demo.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_interpose_demo.ko'
adb shell su -c 'dmesg | grep neverc_krt_interpose_demo'
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
adb shell su -c 'insmod /data/local/tests/nvk_interpose_demo.ko'
```

Новые строки появятся в терминале 1 в момент загрузки. Ctrl+C — остановка.

Примечание: на некоторых сборках Android нет `dmesg -w`; для `/proc/kmsg` нужен root, но для отладки загрузки модулей это надёжнее.

## Выгрузка

```bash
neverc make rmmod
```
