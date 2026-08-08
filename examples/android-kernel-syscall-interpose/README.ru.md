**Языки**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Примеры NeverC](../../docs/examples/README.ru.md)

# Android Kernel Syscall Interpose

Перехват `openat` заменой указателя в `sys_call_table`. Демонстрирует классический перехват системных вызовов на ARM64 GKI ядрах с помощью `neverc_krt_syscall_replace` / `neverc_krt_syscall_restore`.

## API

```c
int neverc_krt_syscall_replace(int nr, neverc_krt_syscall_fn_t new_fn,
                               neverc_krt_syscall_fn_t *orig);
int neverc_krt_syscall_restore(int nr, neverc_krt_syscall_fn_t orig);
```

## Сборка

```bash
cd examples/android-kernel-syscall-interpose
neverc make          # debug: -g (по умолчанию при первой сборке)
neverc make release  # release: -O2 --strip
neverc make debug    # вернуться к debug
```

Другой профиль ядра можно выбрать, например, командой
`neverc make KERNEL=612 release`. Makefile сохраняет `KERNEL` и `PROFILE`,
поэтому последующие `make push`/`run` используют выбранный артефакт.

Release-strip встроен в NeverC и ограничен безопасной для модуля ядра
политикой. Он удаляет DWARF, `.comment` и ненужные релокациям приватные или
неопределённые имена, но сохраняет таблицы символов/строк ET_REL, релокации,
импорты, глобальные определения, `__versions`, `.codetag.alloc_tags` и ABI
загрузчика. Это не strip-all и не обфускация; нужные релокациям имена могут
остаться. Подписывайте модуль только после strip. Не выполняйте strip в
`clean`, не применяйте `llvm-strip --strip-all` к `.ko` и не удаляйте вслепую
`.codetag.alloc_tags` или `__codetag_*`.

## Развёртывание и запуск

```bash
neverc make run
```

Или вручную:

```bash
adb push nvk_syscall_interpose.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_syscall_interpose.ko'
adb shell su -c 'dmesg | grep neverc_krt_syscall'
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
adb shell su -c 'insmod /data/local/tests/nvk_syscall_interpose.ko'
```

Новые строки появятся в терминале 1 в момент загрузки. Ctrl+C — остановка.

Примечание: на некоторых сборках Android нет `dmesg -w`; для `/proc/kmsg` нужен root, но для отладки загрузки модулей это надёжнее.

## Выгрузка

```bash
neverc make rmmod
```
