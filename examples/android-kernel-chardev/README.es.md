**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Ejemplos NeverC](../../docs/examples/README.es.md)

# Dispositivo de caracteres kernel Android

Dispositivo de caracteres misc con interfaz ioctl y página de estado `/proc`. Demuestra `misc_register`, despacho de comandos ioctl y entrada proc basada en `seq_file` — el patrón IPC estándar usuario↔kernel en Android.

## Compilación

```bash
cd examples/android-kernel-chardev
neverc make
```

Cambie `KERNEL` a `515`, `601`, `606`, `612` o `618` para otras versiones.

## Despliegue y ejecución

```bash
neverc make run
```

O manualmente:

```bash
adb push nvk_chardev.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_chardev.ko'
adb shell su -c 'dmesg | grep neverc_krt_chardev'
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
adb shell su -c 'insmod /data/local/tests/nvk_chardev.ko'
```

Las líneas nuevas aparecen en el terminal 1 al cargar. Ctrl+C para detener.

Nota: en algunas builds de Android falta `dmesg -w`; `/proc/kmsg` requiere root pero sigue la salida del kernel en directo de forma fiable.

## Descarga

```bash
neverc make rmmod
```

O manualmente:

```bash
adb shell su -c 'rmmod neverc_krt_chardev'
```
