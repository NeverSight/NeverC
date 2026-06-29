**Langues** : [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android Kernel Function Hook

Hook de `do_faccessat` à son point d'entrée avec `neverc_krt_hook_register`. Démontre :

- **Chaînage automatique** : plusieurs handlers sur la même cible, exécutés par priorité
- **Appel de l'original** : le handler reçoit un pointeur `orig` pour appeler la fonction originale
- **Contrôle de priorité** : valeur plus basse = exécution en premier ; utiliser des valeurs négatives pour passer avant d'autres hooks
- **Coexistence** : fonctionne même si la cible est déjà hookée par un autre module

## API

```c
int neverc_krt_hook_register(void *target, void *handler, int priority,
                             void **orig, struct neverc_krt_hook_ref *ref);
int neverc_krt_hook_unregister(struct neverc_krt_hook_ref *ref);
```

Signature du handler :

```c
long my_hook(void *orig, void *a0, void *a1, void *a2, void *a3, void *a4, void *a5);
```

## Compilation

```bash
cd examples/android-kernel-inline-hook
neverc make
```

Changer `KERNEL` en `515`, `601`, `606` ou `612` pour d'autres versions du noyau.

## Déploiement et exécution

```bash
neverc make run
```

Ou manuellement :

```bash
adb push nvk_hook_demo.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_hook_demo.ko'
adb shell su -c 'dmesg | grep neverc_krt_hook_demo'
```

## Déchargement

```bash
neverc make rmmod
```
