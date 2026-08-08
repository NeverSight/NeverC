**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Ejemplos NeverC](../../docs/examples/README.es.md)

# Demo completa SDK kernel Android

Integración completa del SDK — inicializa todos los subsistemas NVK y los expone a través de una interfaz de comandos netlink. Implementación de referencia para módulos en producción. Incluye: motor de interpose, wrappers de credenciales, visibilidad de módulos, control de política SELinux, enumeración de procesos, inspección VMA, I/O de archivos, detección de entorno y estadísticas.

## Compilación

```bash
cd examples/android-kernel-full
neverc make
```

Cambie `KERNEL` a `515`, `601`, `606`, `612` o `618` para otras versiones.

## Despliegue y ejecución

```bash
neverc make run
```

O manualmente:

```bash
adb push nvk_full.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_full.ko'
adb shell su -c 'dmesg | grep neverc_krt_full'
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
adb shell su -c 'insmod /data/local/tests/nvk_full.ko'
```

Las líneas nuevas aparecen en el terminal 1 al cargar. Ctrl+C para detener.

Nota: en algunas builds de Android falta `dmesg -w`; `/proc/kmsg` requiere root pero sigue la salida del kernel en directo de forma fiable.

## Descarga

```bash
neverc make rmmod
```

O manualmente:

```bash
adb shell su -c 'rmmod neverc_krt_full'
```
