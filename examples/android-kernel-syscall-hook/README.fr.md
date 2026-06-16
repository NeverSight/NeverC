**Langues**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Hook syscall noyau Android

Remplacement de table syscall sur `openat`. Par défaut : échange d'entrée. Avec `-DNVK_SYSCALL_INLINE_HOOK` : patche le prologue du handler. Démontre `nvk_syscall_replace`/`nvk_syscall_restore` et les numéros syscall arm64.

## Construction

```bash
cd examples/android-kernel-syscall-hook
neverc make
```

Changez `KERNEL` en `515`, `601`, `606` ou `612` pour d'autres versions.

## Déploiement et exécution

```bash
neverc make run
```

Ou manuellement :

```bash
adb push nvk_syscall_hook.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_syscall_hook.ko'
adb shell su -c 'dmesg | grep nvk_syscall_hook'
```

## Déchargement

```bash
neverc make rmmod
```

Ou manuellement :

```bash
adb shell su -c 'rmmod nvk_syscall_hook'
```
