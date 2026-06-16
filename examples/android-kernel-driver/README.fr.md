**Langues**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Modèle de pilote noyau Android

Modèle de pilote avec résolution dynamique de symboles via `kallsyms_lookup_name`. N'importe que `register_kprobe`/`unregister_kprobe` (ABI GKI stable). Source unique compatible GKI 5.10–6.12.

## Construction

```bash
cd examples/android-kernel-driver
neverc make
```

Changez `KERNEL` en `515`, `601`, `606` ou `612` pour d'autres versions.

## Déploiement et exécution

```bash
neverc make run
```

Ou manuellement :

```bash
adb push nvk_driver.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_driver.ko'
adb shell su -c 'dmesg | grep nvk_driver'
```

## Déchargement

```bash
neverc make rmmod
```

Ou manuellement :

```bash
adb shell su -c 'rmmod nvk_driver'
```
