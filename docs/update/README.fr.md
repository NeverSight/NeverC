**Langues**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Index de la documentation](../README.fr.md) · [← Projet NeverC](../../README.md)

# `neverc update`

Met à jour une **installation release** de NeverC pour que le compilateur et
chaque runtime de cross-compilation **déjà installé** passent ensemble à
**une balise de release concrète**. `neverc upgrade` est un alias.

Destiné aux installs via `install.sh` (souvent sous `~/.neverc`). Ne met **pas**
à jour un arbre de build CMake/Ninja — changez le PATH et reconstruisez ; voir
[Développement local](../local-dev/README.fr.md).

## Syntaxe

```text
neverc update
neverc update <version>
neverc update --version <version>
neverc update --help
```

Exemples :

```bash
neverc update                 # dernière release complète pour cet hôte
neverc update v3389.1.2       # balise exacte (montée ou descente)
neverc update 3389.1.2        # le « v » initial est optionnel
neverc upgrade                # identique à neverc update
```

`-y` / `--yes` sont acceptés pour les scripts ; la mise à jour est non interactive.

## Périmètre

| Composant | Comportement |
|-----------|--------------|
| Compilateur (`bin/`, `lib/`, `pluginsdk/`) | Remplacé si la balise cible diffère |
| Runtimes déjà sous `runtime/` | Seuls les cibles **déjà installées** sont re-téléchargées et épinglées |
| Runtimes absents | **Pas** installés automatiquement — [`neverc runtime install`](../runtime/README.fr.md) |

## Modèle de sûreté

1. Verrou exclusif sous `<install>/.neverc-update.lock`.
2. Résolution de la balise cible.
3. Téléchargement de `SHA256SUMS` et des archives, puis vérification.
4. Extraction / validation en staging, puis commit ; échec → rollback.

En cas de mauvaise release runtime, indiquez une balise plus ancienne :

```bash
neverc update v3389.0.1
```

## Contraintes

- Uniquement une racine d'installation release (souvent `~/.neverc`). Refuse les racines FS et les arbres CMake.
- La plateforme hôte doit correspondre à un asset compilateur publié.
- Sous Windows, un processus d'aide court peut remplacer `neverc.exe` après la sortie.

## Commandes associées

| Commande | Usage |
|----------|-------|
| [`neverc runtime`](../runtime/README.fr.md) | Sysroots individuels sans changer le compilateur |
| [`neverc run`](../run/README.fr.md) | Compile-et-exécute un binaire temporaire |
| [`neverc build` / `make`](../build/README.fr.md) | Pilote des Makefile d'exemples / projets |
| `neverc update --help` | Aide intégrée |
