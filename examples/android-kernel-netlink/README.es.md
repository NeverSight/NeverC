**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Netlink kernel Android

Canal IPC netlink bidireccional. Crea un socket netlink para comunicación usuario↔kernel. Soporta PING (retorna PONG), VERSION (cadena de versión del kernel) y ECHO. Demuestra `nvk_nl_open`, `nvk_nl_reply` y patrón dispatch-callback.

## Compilación

```bash
cd examples/android-kernel-netlink
neverc make
```

Cambie `KERNEL` a `515`, `601`, `606` o `612` para otras versiones.

## Despliegue y ejecución

```bash
neverc make run
```

O manualmente:

```bash
adb push nvk_netlink.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_netlink.ko'
adb shell su -c 'dmesg | grep nvk_netlink'
```

## Descarga

```bash
neverc make rmmod
```

O manualmente:

```bash
adb shell su -c 'rmmod nvk_netlink'
```
