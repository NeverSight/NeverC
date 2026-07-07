**Lingue**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Modulo lowvis kernel Android

Demo occultamento modulo. Flag: nessuno=nascondere lista base, `-DNVK_LOWVIS_HIDE`=nascondere completo (lista+sysfs+proc), `-DNVK_LOWVIS_FULL_HIDE`=esteso (dmesg+PID+mount+maps), `-DNVK_LOWVIS_ROOT`=concedere root, `-DNVK_LOWVIS_SELINUX`=modalità permissiva.

## Compilazione

```bash
cd examples/android-kernel-lowvis
neverc make
```

Cambiare `KERNEL` in `515`, `601`, `606`, `612` o `618` per altre versioni.

## Distribuzione ed esecuzione

```bash
neverc make run
```

Oppure manualmente:

```bash
adb push nvk_lowvis.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_lowvis.ko'
adb shell su -c 'dmesg | grep nvk_lowvis'
```

## Scaricamento

```bash
neverc make rmmod
```

Oppure manualmente:

```bash
adb shell su -c 'rmmod nvk_lowvis'
```
