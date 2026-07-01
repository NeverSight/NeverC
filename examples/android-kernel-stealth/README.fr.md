**Langues**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Module furtif noyau Android

Démo de dissimulation de module. Drapeaux : aucun=masquage liste basique, `-DNVK_STEALTH_HIDE`=masquage complet (liste+sysfs+proc), `-DNVK_STEALTH_FULL_HIDE`=étendu (dmesg+PID+mount+maps), `-DNVK_STEALTH_ROOT`=accorder root, `-DNVK_STEALTH_SELINUX`=mode permissif.

## Construction

```bash
cd examples/android-kernel-stealth
neverc make
```

Changez `KERNEL` en `515`, `601`, `606`, `612` ou `618` pour d'autres versions.

## Déploiement et exécution

```bash
neverc make run
```

Ou manuellement :

```bash
adb push nvk_stealth.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_stealth.ko'
adb shell su -c 'dmesg | grep nvk_stealth'
```

## Déchargement

```bash
neverc make rmmod
```

Ou manuellement :

```bash
adb shell su -c 'rmmod nvk_stealth'
```
