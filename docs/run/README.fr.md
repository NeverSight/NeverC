**Langues** : [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Index de la documentation](../README.fr.md) · [← Projet NeverC](../../README.md)

# `neverc run`

Compile un programme C ou NeverC en un **exécutable temporaire**, l'exécute sur l'**hôte local**, renvoie son code de sortie, puis supprime l'artefact. Le flux est volontairement proche de `go run`.

Pour conserver le binaire, le distribuer ou le déboguer, utilisez l'invocation normale du compilateur (`neverc ... -o output`).

## Syntaxe

```text
neverc run [options compilateur] file.c [file2.nc ...] [arguments programme...]
neverc run [arguments compilateur...] -- [arguments programme...]
```

`neverc run --help` affiche aussi un résumé intégré.

## Analyse des arguments

`neverc run` sépare les arguments en **invocation compilateur** et **arguments programme** optionnels selon l'une de ces règles.

### Séparation par défaut (style Go)

1. Parcourir de gauche à droite jusqu'au premier argument se terminant par `.c` ou `.nc` sans commencer par `-`.
2. **Tout, y compris la série continue de fichiers `.c`/`.nc` (et tout ce qui la précède)**, est transmis au compilateur.
3. **Tout après** cette série va à `argv` du programme temporaire.

Exemples :

```bash
neverc run -O2 -fbuiltin-string hello.c
neverc run -O2 main.c helper.nc -- --verbose two words
neverc run -DGENERATED=.c -O2 main.c argument
```

Notes :

- Seuls `.c` et `.nc` comptent comme sources run. Un flag `-DGENERATED=.c` reste côté compilateur.
- Plusieurs sources produisent un seul binaire temporaire, comme un link multi-fichiers normal.

### Séparateur `--` explicite

Quand le compilateur a besoin d'arguments **après** la liste des sources (flags de link, entrées non source, `-x c -`, etc.), placez `--` entre la fin compilateur et les arguments programme :

```bash
neverc run hello.c helper.o -lm -- arg.c -x
neverc run hello.c -O1 -- x
```

Tout avant `--` est transmis à `neverc` (plus un `-o <temp>` interne). Tout après devient des arguments programme.

## Comportement à l'exécution

| Sujet | Comportement |
|-------|--------------|
| Répertoire de travail | Le programme temporaire s'exécute dans le **répertoire courant** |
| Environnement | Hérite de l'environnement courant (`PATH`, variables exportées, etc.) |
| E/S standard | stdin/stdout/stderr connectés au processus temporaire |
| Code de sortie | En succès, le code du **programme** ; en échec de compilation, celui du **compilateur** sans lancer le programme |
| Fichiers temporaires | L'exécutable vit sous `neverc-run-*` ; le répertoire est supprimé après exécution. Un échec de nettoyage est signalé séparément. |

## Exemples

```bash
neverc run -O2 -fbuiltin-string hello.c
neverc run -fbuiltin-string greet.c -- Alice "two words"
neverc run -O2 main.c util.nc -- --port 8080
neverc run app.c extra.o -lm -- --config prod.json
```

## Limites et réserves

- **Exécution hôte uniquement.** Les flags de cross-compilation (`-target ...`) peuvent compiler, mais le binaire temporaire s'exécute toujours localement.
- **Pas d'artefact persistant.** Le binaire est supprimé à la fin — utilisez `neverc ... -o out` pour déboguer.
- **Même toolchain que `neverc`.** La commande réinvoque le même binaire `neverc`.
- **Sources `.nc`.** Mêmes règles que `.c` ; les extensions NeverC s'appliquent automatiquement.

## Commandes associées

| Commande | Quand l'utiliser |
|----------|------------------|
| `neverc file.c -o out` | Conserver le binaire, cross-compiler, scripts de build |
| [`neverc build` / `neverc make`](../build/README.fr.md) | Builds compatibles GNU Make pilotés par un Makefile |
| `neverc run --help` | Résumé intégré |
