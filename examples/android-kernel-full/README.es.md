**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Demo completa SDK kernel Android

Integración completa del SDK — inicializa todos los subsistemas NVK y los expone a través de una interfaz de comandos netlink. Implementación de referencia para módulos en producción. Incluye: motor de hooks, credenciales, ocultación de módulos, SELinux, enumeración de procesos, inspección VMA, I/O de archivos, detección de entorno y estadísticas.

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
adb shell su -c 'dmesg | grep nvk_full'
```

## Descarga

```bash
neverc make rmmod
```

O manualmente:

```bash
adb shell su -c 'rmmod nvk_full'
```
