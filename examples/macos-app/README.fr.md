**Langues** : [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Exemples NeverC](../../docs/examples/README.fr.md)

# Exemple d'application macOS

Un exécutable natif macOS Mach-O compilé de manière croisée avec NeverC. Démontre sysctl, uname et les API du noyau Mach pour l'introspection du système et des processus. Compilation depuis macOS, Windows ou Linux — sans Xcode.

## Compilation

Depuis le dépôt (cible par défaut : `arm64-apple-macos`) :

```bash
cd examples/macos-app
neverc make
```

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
neverc --target=arm64-apple-macos -Wall -o macos-app main.c
```

## Exécution

```bash
./macos-app
```

## Fonctionnalités

- Interrogation des informations noyau via `uname`
- Lecture des détails matériels via `sysctl` (modèle, nombre de CPU, taille mémoire, taille de page)
- Affichage de l'identité du processus (`getpid`, `getppid`, `getuid`)
- Récupération des informations hôte Mach (`host_info`) et statistiques mémoire de tâche (`task_info`)
