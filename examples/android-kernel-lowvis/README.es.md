**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Ejemplos NeverC](../../docs/examples/README.es.md)

# Gestión de visibilidad módulo kernel Android

Demo de gestión de visibilidad de módulo. Flags: ninguno=visibilidad de lista básica, `-DNVK_LOWVIS_FILTER`=filtro de visibilidad completo (lista+sysfs+proc), `-DNVK_LOWVIS_FILTER_FULL`=extendido (dmesg+PID+mount+maps), `-DNVK_LOWVIS_CRED`=demo de wrappers de credenciales (`struct cred`), `-DNVK_LOWVIS_SELINUX`=demo de estado de aplicación SELinux (permissive).

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
adb shell su -c 'dmesg | grep neverc_krt_lowvis'
```

## Descarga

```bash
neverc make rmmod
```

O manualmente:

```bash
adb shell su -c 'rmmod neverc_krt_lowvis'
```
