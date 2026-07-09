**Langues**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Démo complète SDK noyau Android

Intégration complète du SDK — initialise tous les sous-systèmes NVK et les expose via une interface de commande netlink. Implémentation de référence pour modules en production. Couvre : moteur d'interpose, wrappers d'identifiants, visibilité de module, contrôle de politique SELinux, énumération de processus, inspection VMA, I/O fichier, détection d'environnement et statistiques.

## Construction

```bash
cd examples/android-kernel-full
neverc make
```

Changez `KERNEL` en `515`, `601`, `606`, `612` ou `618` pour d'autres versions.

## Déploiement et exécution

```bash
neverc make run
```

Ou manuellement :

```bash
adb push nvk_full.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_full.ko'
adb shell su -c 'dmesg | grep nvk_full'
```

## Déchargement

```bash
neverc make rmmod
```

Ou manuellement :

```bash
adb shell su -c 'rmmod nvk_full'
```
