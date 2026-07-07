**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android Kernel Multi-File Module

Modulo kernel NeverC multi-archivo. Puntos clave:

- **Bootstrap unico**: `NEVERC_KRT_BOOTSTRAP()` solo se llama una vez en `module_init`
- **Estado compartido**: el compilador promueve todo el estado `neverc_krt_*` a linkage `weak_odr`, todos los `.c` comparten el mismo resolver, cache y estado
- **Arquitectura dividida**: `main.c` (init/exit), `interposes.c` (logica de interpose), `utils.c` (helpers)

## Compilar

```bash
cd examples/android-kernel-multifile
neverc make
```

Cambiar `KERNEL` a `515`, `601`, `606`, `612` o `618` para otras versiones del kernel.

## Despliegue y ejecucion

```bash
neverc make run
```

O manualmente:

```bash
adb push nvk_multi.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_multi.ko'
adb shell su -c 'dmesg | grep neverc_krt_multi'
```

## Descargar

```bash
neverc make rmmod
```
