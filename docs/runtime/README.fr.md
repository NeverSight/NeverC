**Langues**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Index de la documentation](../README.fr.md) · [← Projet NeverC](../../README.md)

# `neverc runtime`

Gère les paquets **runtime de cross-compilation** (sysroots / SDK) téléchargés
depuis [GitHub Releases](https://github.com/NeverSight/NeverC/releases). Ils
vivent sous `<NeverC-root>/runtime/` (souvent `~/.neverc/runtime/`).

Préférez `neverc runtime install …` à l'extraction manuelle des zip
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
|--------|-------------------------|
| `windows-x64` | `windows/x64` (+ shared `windows/shared`) |
| `windows-arm64` | `windows/arm64` (+ shared `windows/shared`) |
| `linux-x64` | `linux/x64` |
| `linux-arm64` | `linux/arm64` |
| `macos-arm64` | `macos/arm64` |
| `android-arm64` | `android/arm64` |
| `android-kernel-arm64` | `android/kernel` |

## Exemples

```bash
neverc runtime install windows-x64
neverc runtime install all
neverc runtime update linux-arm64 --version v3389.1.2
neverc runtime remove macos-arm64
neverc runtime list
```

## Sous-commandes

- **`install`** : installe une cible à la **balise du compilateur** par défaut (ou `--version`). Même balise → succès ; balise différente → confirmation `[Y/n]`.
- **`install all`** : installe toutes les cibles **manquantes** ; les déjà installées sont ignorées.
- **`update` / `upgrade`** : force le téléchargement sans invite. Défaut : **latest**.
- **`remove` / `uninstall`** : supprime le répertoire et met à jour `manifest.json`.
- **`list` / `ls`** : état d'installation et balise du compilateur.

## Règles de version

| Commande | Défaut sans `--version` |
|----------|-------------------------|
| `install` / `install all` | Balise release du compilateur |
| `update` | Dernière release publiant cet asset runtime |

Balises `vMAJOR.MINOR.PATCH` ; vérification via `SHA256SUMS` avant extraction.

## Lien avec `neverc update`

- `neverc runtime …` ne change que les **sysroots**.
- [`neverc update`](../update/README.fr.md) aligne **compilateur + runtimes déjà installés**.

Après `neverc update`, n'installez que les **nouvelles** cibles avec `runtime install`.

## Commandes associées

| Commande | Usage |
|----------|-------|
| [`neverc update`](../update/README.fr.md) | Compilateur + runtimes installés ensemble |
| [`neverc build` / `make`](../build/README.fr.md) | Exemples de cross-compilation |
| [Examples](../examples/README.fr.md) | Makefile avec `--target=…` |
| `neverc runtime --help` | Aide intégrée |
