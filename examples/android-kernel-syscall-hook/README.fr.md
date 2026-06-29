**Langues** : [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android Kernel Syscall Hook

Hook de `openat` par remplacement de son pointeur dans `sys_call_table`. Démontre l'interception classique de syscall sur les noyaux ARM64 GKI avec `neverc_krt_syscall_replace` / `neverc_krt_syscall_restore`.

## API

```c
int neverc_krt_syscall_replace(int nr, neverc_krt_syscall_fn_t new_fn,
                               neverc_krt_syscall_fn_t *orig);
int neverc_krt_syscall_restore(int nr, neverc_krt_syscall_fn_t orig);
```

## Compilation

```bash
cd examples/android-kernel-syscall-hook
neverc make
```

Changer `KERNEL` en `515`, `601`, `606` ou `612` pour d'autres versions du noyau.

## Déploiement et exécution

```bash
neverc make run
```

Ou manuellement :

```bash
adb push nvk_syscall_hook.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_syscall_hook.ko'
adb shell su -c 'dmesg | grep neverc_krt_syscall'
```

## Déchargement

```bash
neverc make rmmod
```
