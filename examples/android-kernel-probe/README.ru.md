**Языки**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Примеры NeverC](../../docs/examples/README.ru.md)

# Android Kernel Probe

Перехват произвольной инструкции внутри `do_faccessat` (не точки входа) с помощью `neverc_krt_probe_register`. Демонстрирует:

- **Хук по произвольному адресу**: перехват любой инструкции, не только входов функций
- **Полный контекст регистров**: чтение/запись всех GPR через `neverc_krt_reg_ctx`
- **Автоматическая цепочка**: несколько обработчиков на одном адресе, выполняемых по приоритету
- **Управление потоком**: `NEVERC_KRT_CTX_SKIP` для прерывания, `NEVERC_KRT_CTX_REDIRECT` для перенаправления

## API

```c
int neverc_krt_probe_register(void *addr, neverc_krt_ctx_handler_t handler,
                              int priority, struct neverc_krt_probe_ref *ref);
int neverc_krt_probe_unregister(struct neverc_krt_probe_ref *ref);
```

Сигнатура обработчика:

```c
void my_probe(neverc_krt_reg_ctx *ctx);
```

## Сборка

```bash
cd examples/android-kernel-probe
neverc make
```

Измените `KERNEL` на `515`, `601`, `606` или `612` для других версий ядра.

## Развертывание и запуск

```bash
neverc make run
```

Или вручную:

```bash
adb push nvk_probe.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_probe.ko'
adb shell su -c 'dmesg | grep neverc_krt_probe_demo'
```

## Выгрузка

```bash
neverc make rmmod
```
