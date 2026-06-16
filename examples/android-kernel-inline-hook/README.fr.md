**Langues**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Hook inline noyau Android

Hook inline sur `do_faccessat`. Par défaut : remplacement simple avec trampoline. Avec `-DNVK_CONTEXT_HOOK` : hook contextuel recevant l'état complet des registres `nvk_reg_ctx`. Démontre le patching sûr BTI/PAC, la relocation relative au PC et le trampoline cohérent D-cache→I-cache.

## Construction

```bash
cd examples/android-kernel-inline-hook
neverc make
```

Changez `KERNEL` en `515`, `601`, `606` ou `612` pour d'autres versions.

## Déploiement et exécution

```bash
neverc make run
```

Ou manuellement :

```bash
adb push nvk_inline_hook.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_inline_hook.ko'
adb shell su -c 'dmesg | grep nvk_inline_hook'
```

## Déchargement

```bash
neverc make rmmod
```

Ou manuellement :

```bash
adb shell su -c 'rmmod nvk_inline_hook'
```
