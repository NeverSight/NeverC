**Языки**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android Kernel Function Hook

Хук `do_faccessat` в точке входа с помощью `neverc_krt_hook_register`. Демонстрирует:

- **Автоматическая цепочка**: несколько обработчиков на одной цели, выполняемых по приоритету
- **Вызов оригинала**: обработчик получает указатель `orig` для вызова исходной функции
- **Управление приоритетом**: меньшее значение = выполнение раньше; отрицательные значения для запуска перед другими хуками
- **Сосуществование**: работает даже если цель уже перехвачена другим модулем

## API

```c
int neverc_krt_hook_register(void *target, void *handler, int priority,
                             void **orig, struct neverc_krt_hook_ref *ref);
int neverc_krt_hook_unregister(struct neverc_krt_hook_ref *ref);
```

Сигнатура обработчика:

```c
long my_hook(void *orig, void *a0, void *a1, void *a2, void *a3, void *a4, void *a5);
```

## Сборка

```bash
cd examples/android-kernel-inline-hook
neverc make
```

Измените `KERNEL` на `515`, `601`, `606` или `612` для других версий ядра.

## Развёртывание и запуск

```bash
neverc make run
```

Или вручную:

```bash
adb push nvk_hook_demo.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_hook_demo.ko'
adb shell su -c 'dmesg | grep neverc_krt_hook_demo'
```

## Выгрузка

```bash
neverc make rmmod
```
