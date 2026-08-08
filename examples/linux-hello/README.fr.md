**Langues** : [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Exemples NeverC](../../docs/examples/README.fr.md)

# Exemple Linux Hello World

Un programme C minimal cross-compilé vers un ELF Linux avec NeverC. Compilation depuis macOS, Windows ou Linux — aucune chaîne d'outils cible requise.

NeverC embarque un sysroot Linux (Ubuntu 22.04, glibc 2.35) dans `runtime/linux/`, permettant le prétraitement, la compilation, l'optimisation (auto-LTO) et l'édition de liens via l'éditeur intégré en une seule invocation.

## Compilation

Depuis le dépôt (cible par défaut : `x86_64-linux-gnu`) :

```bash
cd examples/linux-hello
neverc make          # debug : -g (première construction par défaut)
neverc make release  # release : -O2 --strip
neverc make debug    # retour au profil debug
```

Le Makefile mémorise `PROFILE`, donc les `neverc make` suivants gardent
le même choix debug/release. La version release utilise `--strip` intégré
à NeverC : métadonnées de débogage et noms de symboles statiques inutiles
sont retirés, les noms ABI dynamiques/chargeur nécessaires restent.
Voir [Builds de publication](../../docs/release-builds/README.fr.md).


Compilation pour AArch64 :

```bash
neverc make TARGET=aarch64-linux-gnu
```

Avec une version autonome de NeverC :

```bash
neverc make NEVERC=/path/to/neverc
```

## Compilation manuelle (sans Make)

```bash
neverc --target=x86_64-linux-gnu -Wall -o hello main.c
```

## Exécution

Copiez `hello` sur une machine Linux (ou un conteneur Docker) et exécutez :

```bash
chmod +x hello
./hello
```

## Fonctionnalités

- Affiche un message d'accueil avec les arguments de la ligne de commande
- Démontre `printf`, `strncpy`, `strlen`, `atoi` de la libc embarquée
- Transformation XOR d'une chaîne pour vérifier les opérations entier/caractère de base
