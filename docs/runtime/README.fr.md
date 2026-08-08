**Langues**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Index de la documentation](../README.fr.md) · [← Projet NeverC](../../README.md)

# `neverc runtime`

Gère les paquets de **runtime de compilation croisée** (sysroots / SDK) issus de
[GitHub Releases](https://github.com/NeverSight/NeverC/releases). Ils vivent sous
`<NeverC-root>/runtime/` à côté du compilateur (installation par défaut :
`~/.neverc/runtime/`).

Préférez `neverc runtime install …` au déballage manuel de
`neverc-runtime-<target>.zip`.

## Syntaxe

```text
neverc runtime install <target> [--version <tag>]
neverc runtime install all [--version <tag>]
neverc runtime update <target> [--version <tag>]
neverc runtime remove <target>
neverc runtime list
neverc runtime --help
```

Alias : `upgrade` → `update` ; `uninstall` → `remove` ; `ls` → `list`.

## Cibles disponibles

| Cible | Disposition sous `runtime/` |
|-------|-----------------------------|
| `windows-x64` | `windows/x64` (+ partagé `windows/shared`) |
| `windows-arm64` | `windows/arm64` (+ partagé `windows/shared`) |
| `linux-x64` | `linux/x64` |
| `linux-arm64` | `linux/arm64` |
| `macos-arm64` | `macos/arm64` |
| `android-arm64` | `android/arm64` |
| `android-kernel-arm64` | `android/kernel` |

## Sous-commandes

### `install`

Installe une cible avec le **tag de release du compilateur** par défaut (ou
`--version <tag>`). Nom de l’actif : `neverc-runtime-<target>.zip`.

```bash
neverc runtime install windows-x64
neverc runtime install linux-arm64 --version v3389.1.2
```

Si la cible est déjà installée :

- Même tag → signaler et quitter avec succès.
- Tag différent / inconnu → confirmer `[Y/n]` avant de réinstaller.

### `install all`

Installe **toutes les cibles manquantes** du catalogue à la version du
compilateur (ou `--version`). Les cibles déjà installées sont ignorées ; pour
changer le pin, relancez `install` sur une seule cible.

```bash
neverc runtime install all
```

### `update` / `upgrade`

Force le téléchargement d’une cible sans invite interactive. La version par
défaut est **latest** (contrairement à `install`, qui suit le tag du
compilateur). Passez `--version` pour figer.

```bash
neverc runtime update windows-x64
neverc runtime update android-arm64 --version v3389.1.2
```

### `remove` / `uninstall`

Supprime le répertoire d’une cible installée et met à jour
`runtime/manifest.json`.

```bash
neverc runtime remove linux-x64
```

### `list` / `ls`

Affiche chaque cible du catalogue comme installée (avec tag enregistré) ou non,
plus le tag vivant du compilateur.

```bash
neverc runtime list
```

## Règles de version

| Commande | Défaut sans `--version` |
|----------|-------------------------|
| `install` / `install all` | Tag de release du compilateur |
| `update` | Dernière release qui publie cet actif runtime |

Les tags ressemblent à `vMAJOR.MINOR.PATCH`. Les archives sont vérifiées contre
`SHA256SUMS` de la release avant extraction.

## Relation avec `neverc update`

- `neverc runtime …` ne modifie que les **sysroots**.
- [`neverc update`](../update/README.fr.md) déplace le **compilateur et tous
  les runtimes déjà installés** vers un tag en une seule transaction.

Après une mise à niveau du compilateur avec `neverc update`, les runtimes
installés sont déjà alignés ; n’utilisez `runtime install` que pour les cibles
**nouvelles**.

## Commandes associées

| Commande | Quand l’utiliser |
|----------|------------------|
| [`neverc update`](../update/README.fr.md) | Monter/descendre compilateur + runtimes installés ensemble |
| [`neverc build` / `make`](../build/README.fr.md) | Construire les exemples de compilation croisée contre ces sysroots |
| [Examples](../examples/README.fr.md) | `Makefile`s d’exemple avec `--target=…` |
| `neverc runtime --help` | Résumé d’usage intégré |
