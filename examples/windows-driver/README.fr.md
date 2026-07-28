**Langues**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Exemples NeverC](../../docs/examples/README.fr.md)

# Exemple de pilote noyau Windows

Un pilote noyau WDM minimal construit avec NeverC. Cible **x64** par défaut, et
peut également être compilé pour ARM64. Compilation croisée depuis macOS / Linux.

NeverC est un compilateur tout-en-un — un seul appel gère le prétraitement,
la compilation, l'optimisation (auto-LTO) et l'édition de liens via l'éditeur
de liens intégré.

## Compilation

Depuis le dépôt :

```bash
cd examples/windows-driver
neverc make
```

Cela produit `ExampleDriver-x64.sys`. Pour compiler pour ARM64, ou pour les deux :

```bash
neverc make ARCH=arm64
neverc make all-arch
```

Depuis une version autonome de NeverC :

```bash
neverc make NEVERC=/path/to/neverc
```

Le résultat est `ExampleDriver-<arch>.sys` (optimisé auto-LTO).
La compilation par défaut inclut `-g` pour le débogage ; **les versions de
production doivent supprimer `-g`** pour retirer les symboles de débogage et
réduire la taille du binaire (~38 Ko → ~3 Ko).

## Compilation manuelle (sans Make)

```bash
neverc --target=x86_64-pc-windows-msvc \
  -g \
  -fms-kernel \
  -Wall -nostdlib -shared \
  -Xlinker --entry=DriverEntry \
  -Xlinker --subsystem=native \
  -Xlinker --nodefaultlib \
  -lntoskrnl -lhal \
  -o ExampleDriver-x64.sys driver.c
```

Pour ARM64, remplacez la cible par `aarch64-pc-windows-msvc` ; rien d'autre ne
change. `-fms-kernel` sélectionne les en-têtes et bibliothèques d'importation du
WDK correspondant à la cible et définit les macros d'architecture attendues par
le WDK, il n'y a donc jamais à les passer à la main.

> `-g` intègre les informations de débogage DWARF dans le PE ; inspectez avec
> `llvm-dwarfdump`. Omettez cette option pour les versions de production afin
> de réduire la taille du binaire.

## Fonctionnalités

- Crée un objet périphérique à `\Device\ExampleDriver`
- Crée un lien symbolique à `\DosDevices\ExampleDriver`
- Gère `IRP_MJ_CREATE`, `IRP_MJ_CLOSE`, `IRP_MJ_DEVICE_CONTROL`
- Affiche les messages de chargement/déchargement via `DbgPrint`

## Chargement (sur une machine de test Windows)

```cmd
sc create ExampleDriver type= kernel binPath= C:\path\to\ExampleDriver-x64.sys
sc start ExampleDriver
sc stop ExampleDriver
sc delete ExampleDriver
```

Activez la signature de test ou utilisez un certificat de signature de code pour
la production.
