**Lingue**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Hook inline kernel Android

Hook inline su `do_faccessat`. Default: sostituzione semplice con trampoline. Con `-DNVK_CONTEXT_HOOK`: hook contestuale che riceve lo stato completo dei registri `nvk_reg_ctx`. Dimostra patching sicuro BTI/PAC, rilocazione relativa al PC e trampoline coerente D-cache→I-cache.

## Compilazione

```bash
cd examples/android-kernel-inline-hook
neverc make
```

Cambiare `KERNEL` in `515`, `601`, `606` o `612` per altre versioni.

## Distribuzione ed esecuzione

```bash
neverc make run
```

Oppure manualmente:

```bash
adb push nvk_inline_hook.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_inline_hook.ko'
adb shell su -c 'dmesg | grep nvk_inline_hook'
```

## Scaricamento

```bash
neverc make rmmod
```

Oppure manualmente:

```bash
adb shell su -c 'rmmod nvk_inline_hook'
```
