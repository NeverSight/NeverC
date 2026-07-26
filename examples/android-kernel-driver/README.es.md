**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Ejemplos NeverC](../../docs/examples/README.es.md)

# Plantilla de controlador de kernel Android

Plantilla de controlador con resolución dinámica de símbolos mediante `kallsyms_lookup_name`. Solo importa `register_kprobe`/`unregister_kprobe` (ABI estable GKI). Fuente única compatible con GKI 5.10–6.12.

## Compilación

```bash
cd examples/android-kernel-driver
neverc make
```

Cambie `KERNEL` a `515`, `601`, `606`, `612` o `618` para otras versiones.

## Despliegue y ejecución

```bash
neverc make run
```

O manualmente:

```bash
adb push nvk_driver.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_driver.ko'
adb shell su -c 'dmesg | grep nvk_driver'
```

## Descarga

```bash
neverc make rmmod
```

O manualmente:

```bash
adb shell su -c 'rmmod nvk_driver'
```
