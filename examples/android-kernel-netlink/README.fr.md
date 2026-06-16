**Langues**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Netlink noyau Android

Canal IPC netlink bidirectionnel. Crée un socket netlink pour la communication utilisateur↔noyau. Supporte PING (retourne PONG), VERSION (version noyau) et ECHO. Démontre `nvk_nl_open`, `nvk_nl_reply` et le pattern dispatch-callback.

## Construction

```bash
cd examples/android-kernel-netlink
neverc make
```

Changez `KERNEL` en `515`, `601`, `606` ou `612` pour d'autres versions.

## Déploiement et exécution

```bash
neverc make run
```

Ou manuellement :

```bash
adb push nvk_netlink.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_netlink.ko'
adb shell su -c 'dmesg | grep nvk_netlink'
```

## Déchargement

```bash
neverc make rmmod
```

Ou manuellement :

```bash
adb shell su -c 'rmmod nvk_netlink'
```
