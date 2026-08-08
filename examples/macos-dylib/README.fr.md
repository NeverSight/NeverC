**Langues** : [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Exemples NeverC](../../docs/examples/README.fr.md)

# Exemple de bibliothèque dynamique macOS

Une bibliothèque dynamique native macOS `.dylib` compilée de manière croisée avec NeverC. Encapsule les interfaces du noyau Mach pour l'introspection des tâches et les opérations de mémoire virtuelle — conçue pour la recherche en sécurité. Compilation depuis macOS, Windows ou Linux — sans Xcode.

## Compilation

Depuis le dépôt (cible par défaut : `arm64-apple-macos`) :

```bash
cd examples/macos-dylib
neverc make          # debug : -g (première construction par défaut)
neverc make release  # release : -O2 --strip
neverc make debug    # retour au profil debug
```

Le Makefile mémorise `PROFILE`, donc les `neverc make` suivants gardent
le même choix debug/release. La version release utilise `--strip` intégré
à NeverC : métadonnées de débogage et noms de symboles statiques inutiles
sont retirés, les noms ABI dynamiques/chargeur nécessaires restent.
Voir [Builds de publication](../../docs/release-builds/README.fr.md).


Compilation pour Intel :

```bash
neverc make TARGET=x86_64-apple-macos
```

Avec une version autonome de NeverC :

```bash
neverc make NEVERC=/path/to/neverc
```

## Compilation manuelle (sans Make)

```bash
neverc --target=arm64-apple-macos -Wall -dynamiclib -o libneverc.dylib lib.c
```

## Fonctionnalités

- Exporte un wrapper `nc_task_basic_info` pour les requêtes Mach `task_info`
- Fournit `nc_vm_read`/`nc_vm_write` pour la lecture/écriture de mémoire virtuelle Mach
- `nc_vm_alloc`/`nc_vm_dealloc` pour l'allocation et la libération de mémoire VM Mach
- Fonction utilitaire de chiffrement XOR et requêtes PID/tâche
