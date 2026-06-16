**Lingue**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Netlink kernel Android

Canale IPC netlink bidirezionale. Crea un socket netlink per comunicazione utente↔kernel. Supporta PING (restituisce PONG), VERSION (stringa versione kernel) e ECHO. Dimostra `nvk_nl_open`, `nvk_nl_reply` e pattern dispatch-callback.

## Compilazione

```bash
cd examples/android-kernel-netlink
neverc make
```

Cambiare `KERNEL` in `515`, `601`, `606` o `612` per altre versioni.

## Distribuzione ed esecuzione

```bash
neverc make run
```

Oppure manualmente:

```bash
adb push nvk_netlink.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_netlink.ko'
adb shell su -c 'dmesg | grep nvk_netlink'
```

## Scaricamento

```bash
neverc make rmmod
```

Oppure manualmente:

```bash
adb shell su -c 'rmmod nvk_netlink'
```
