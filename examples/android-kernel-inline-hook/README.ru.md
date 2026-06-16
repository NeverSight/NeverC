**Языки**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Инлайн-хук ядра Android

Инлайн-хук на `do_faccessat`. По умолчанию: простая замена с трамплином. С `-DNVK_CONTEXT_HOOK`: контекстный хук с полным состоянием регистров `nvk_reg_ctx`. Демонстрирует BTI/PAC-безопасный патчинг, PC-относительную релокацию и когерентный D-cache→I-cache трамплин.

## Сборка

```bash
cd examples/android-kernel-inline-hook
neverc make
```

Измените `KERNEL` на `515`, `601`, `606` или `612` для других версий.

## Развёртывание и запуск

```bash
neverc make run
```

Или вручную:

```bash
adb push nvk_inline_hook.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_inline_hook.ko'
adb shell su -c 'dmesg | grep nvk_inline_hook'
```

## Выгрузка

```bash
neverc make rmmod
```

Или вручную:

```bash
adb shell su -c 'rmmod nvk_inline_hook'
```
