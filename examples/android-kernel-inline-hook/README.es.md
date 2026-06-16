**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Hook inline kernel Android

Hook inline en `do_faccessat`. Por defecto: reemplazo simple con trampolín. Con `-DNVK_CONTEXT_HOOK`: hook de contexto que recibe el estado completo de registros `nvk_reg_ctx`. Demuestra parcheo seguro BTI/PAC, reubicación relativa al PC y trampolín coherente D-cache→I-cache.

## Compilación

```bash
cd examples/android-kernel-inline-hook
neverc make
```

Cambie `KERNEL` a `515`, `601`, `606` o `612` para otras versiones.

## Despliegue y ejecución

```bash
neverc make run
```

O manualmente:

```bash
adb push nvk_inline_hook.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_inline_hook.ko'
adb shell su -c 'dmesg | grep nvk_inline_hook'
```

## Descarga

```bash
neverc make rmmod
```

O manualmente:

```bash
adb shell su -c 'rmmod nvk_inline_hook'
```
