**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Exemple bibliothèque partagée Android

Une bibliothèque partagée `.so` native ARM64 compilée en croisé pour Android avec NeverC. Compilable depuis macOS, Windows ou Linux.

## Compilation

```bash
cd examples/android-so
make
```

## Compilation manuelle

```bash
neverc --target=aarch64-linux-android -Wall -shared -fPIC -ldl -o libneverc.so lib.c
```

## Fonctionnalités

- Fonctions d'aide pour la recherche en sécurité des jeux : requête PID, lecture `/proc/self/maps`, allocation mémoire RWX, chiffrement XOR
- Chargement dynamique de `liblog.so` via `dlopen`
- Démonstration d'allocation mémoire exécutable avec `mmap` + `PROT_EXEC`

