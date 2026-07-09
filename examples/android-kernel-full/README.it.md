**Lingue**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Demo completa SDK kernel Android

Integrazione completa SDK — inizializza tutti i sottosistemi NVK e li espone tramite interfaccia comandi netlink. Implementazione di riferimento per moduli in produzione. Include: motore interpose, wrapper credenziali, visibilità moduli, controllo policy SELinux, enumerazione processi, ispezione VMA, I/O file, rilevamento ambiente e statistiche.

## Compilazione

```bash
cd examples/android-kernel-full
neverc make
```

Cambiare `KERNEL` in `515`, `601`, `606`, `612` o `618` per altre versioni.

## Distribuzione ed esecuzione

```bash
neverc make run
```

Oppure manualmente:

```bash
adb push nvk_full.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_full.ko'
adb shell su -c 'dmesg | grep nvk_full'
```

## Scaricamento

```bash
neverc make rmmod
```

Oppure manualmente:

```bash
adb shell su -c 'rmmod nvk_full'
```
