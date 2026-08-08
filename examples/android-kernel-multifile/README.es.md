**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Ejemplos NeverC](../../docs/examples/README.es.md)

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

## Registro del kernel (en vivo)

En el dispositivo, `cat /proc/kmsg` transmite el ring buffer del kernel en tiempo real — similar a **DbgView** en Windows. Úselo cuando `insmod` falle con un error vago o necesite ver el motivo real del rechazo (vermagic, modversions, tamaño de sección, etc.).

Terminal 1 (dejar en ejecución):

```bash
adb shell
su
cat /proc/kmsg
```

Terminal 2:

```bash
adb shell su -c 'insmod /data/local/tests/nvk_multi.ko'
```

Las líneas nuevas aparecen en el terminal 1 al cargar. Ctrl+C para detener.

Nota: en algunas builds de Android falta `dmesg -w`; `/proc/kmsg` requiere root pero sigue la salida del kernel en directo de forma fiable.

## Descargar

```bash
neverc make rmmod
```
