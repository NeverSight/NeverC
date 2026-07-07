**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Módulo sigiloso kernel Android

Demo de ocultación de módulo. Flags: ninguno=ocultar lista básica, `-DNVK_LOWVIS_HIDE`=ocultar completo (lista+sysfs+proc), `-DNVK_LOWVIS_FULL_HIDE`=extendido (dmesg+PID+mount+maps), `-DNVK_LOWVIS_ROOT`=conceder root, `-DNVK_LOWVIS_SELINUX`=modo permisivo.

## Compilación

```bash
cd examples/android-kernel-lowvis
neverc make
```

Cambie `KERNEL` a `515`, `601`, `606`, `612` o `618` para otras versiones.

## Despliegue y ejecución

```bash
neverc make run
```

O manualmente:

```bash
adb push nvk_lowvis.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_lowvis.ko'
adb shell su -c 'dmesg | grep nvk_lowvis'
```

## Descarga

```bash
neverc make rmmod
```

O manualmente:

```bash
adb shell su -c 'rmmod nvk_lowvis'
```
