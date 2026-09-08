**Langues**: [English](../build.md) | [简体中文](../zh-CN/build.md) | [繁體中文](../zh-TW/build.md) | [日本語](../ja/build.md) | [한국어](../ko/build.md) | [Français](build.md) | [Deutsch](../de/build.md) | [Español](../es/build.md) | [Italiano](../it/build.md) | [Русский](../ru/build.md) | [العربية](../ar/build.md)

[← Index de la documentation](README.md) · [← Projet NeverC](project.md)

# `neverc build` / `neverc make`

NeverC embarque un pilote **compatible GNU Make**. `neverc build` et
`neverc make` sont la même commande : lecture du Makefile, expansion, exécution
des recettes. Les [`examples/`](examples.md) suivent ce flux.

Ce n'est **pas** un outil de projet `neverc.toml`. Passez des options Make
ordinaires et des `VAR=value`.

## Syntaxe

```text
neverc build [options] [target...]
neverc make  [options] [target...]
```

```bash
cd examples/linux-hello
neverc make
neverc make clean
neverc make NEVERC=/path/to/neverc TARGET=aarch64-linux-gnu
```

Liste complète : `neverc make --help`.

## Options

| Option | Signification |
|--------|---------------|
| `-f FILE` | Lire ce Makefile |
| `-j [N]` | Jobs parallèles (`-j` seul = Nb CPU) |
| `-C DIR` | Changer de répertoire avant lecture |
| `-n`, `--dry-run` | Afficher sans exécuter |
| `-k`, `--keep-going` | Continuer après erreur |
| `-s`, `--silent` | Ne pas échoïer les recettes |
| `-B`, `--always-make` | Tout reconstruire |
| `-p` | Afficher la base règles/variables |
| `VAR=VALUE` | Variable en ligne de commande |
| `-h`, `--help` | Afficher l'aide |

## Découverte du Makefile

Sans `-f` : `GNUmakefile` → `makefile` → `Makefile`.

## Surface Make prise en charge (résumé)

Règles et motifs, `.PHONY`, préfixes de recettes, affectations, conditionnels,
`include`/`export`, fonctions courantes (`subst`, `patsubst`, `wildcard`,
`foreach`, `call`, `eval`, `shell`, …). `MAKE_VERSION` annonce `4.3` pour
compatibilité. Sous-ensemble volontaire, pas un GNU Make complet.

## Makefile typique

```make
NEVERC ?= neverc
TARGET  = x86_64-linux-gnu
OUTPUT  = hello
SRCS    = main.c

FLAGS = --target=$(TARGET) -O2

all: $(OUTPUT)

$(OUTPUT): $(SRCS)
	$(NEVERC) $(FLAGS) -o $@ $(SRCS)

clean:
	rm -f $(OUTPUT)

.PHONY: all clean
```

Les exemples de cross passent souvent `ARCH=…` ou `TARGET=…`. Voir
[Examples](examples.md).

## Commandes associées

| Commande | Usage |
|----------|-------|
| `neverc file.c -o out` | Compilation sans Makefile |
| [`neverc run`](run.md) | Compile-et-exécute temporaire |
| [`neverc runtime`](runtime.md) | Installer les sysroots de cross |
| [Binaires et `--strip`](release-builds.md) | Stripper l'image finale |
