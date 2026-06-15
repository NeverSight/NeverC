**Langues**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Exemple Android ELF

Un binaire ELF natif ARM64 compilé en croisé pour Android avec NeverC. Conçu pour être exécuté directement sur des appareils Android rootés via `adb shell`. Compilable depuis macOS, Windows ou Linux — sans Android NDK ni CMake.

NeverC intègre un sysroot Android (NDK r26c, API 21+) dans `runtime/android/`, permettant le prétraitement, la compilation, l'optimisation (LTO automatique) et l'édition de liens en un seul appel.

## Compilation

Depuis le dépôt :

```bash
cd examples/android-elf
neverc make
```

Avec une version autonome de NeverC :

```bash
neverc make NEVERC=/path/to/neverc
```

## Compilation manuelle (sans Make)

```bash
neverc --target=aarch64-linux-android -Wall -fPIE -lm -ldl -llog -o android-elf main.c
```

## Déploiement et exécution

Transférer sur l'appareil et exécuter via adb :

```bash
neverc make run
```

Ou manuellement :

```bash
adb push android-elf /data/local/tests/
adb shell chmod 755 /data/local/tests/android-elf
adb shell /data/local/tests/android-elf
```

## Fonctionnalités

- Affiche les informations de l'appareil (`uname`) et la version du noyau
- Vérifie le statut root/privilèges (`uid`/`euid`, chemins `su`)
- Charge dynamiquement `liblog.so` et appelle `__android_log_print`
- Lit `/proc/self/maps` pour afficher la disposition mémoire
- Démontre `dlopen`/`dlsym`, `readlink`, `fopen` sur Android
