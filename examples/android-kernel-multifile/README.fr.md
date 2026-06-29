**Langues** : [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android Kernel Multi-File Module

Module noyau NeverC multi-fichiers. Points cles :

- **Bootstrap unique** : `NEVERC_KRT_BOOTSTRAP()` n'est appele qu'une fois dans `module_init`
- **Etat partage** : le compilateur promeut tout l'etat `neverc_krt_*` en linkage `weak_odr`, tous les `.c` partagent le meme resolveur, cache et etat
- **Architecture divisee** : `main.c` (init/exit), `hooks.c` (logique hook), `utils.c` (helpers)

## Compilation

```bash
cd examples/android-kernel-multifile
neverc make
```

Changer `KERNEL` en `515`, `601`, `606` ou `612` pour d'autres versions du noyau.

## Deploiement et execution

```bash
neverc make run
```

Ou manuellement :

```bash
adb push nvk_multi.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_multi.ko'
adb shell su -c 'dmesg | grep neverc_krt_multi'
```

## Dechargement

```bash
neverc make rmmod
```
