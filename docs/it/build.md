**Lingue**: [English](../build.md) | [简体中文](../zh-CN/build.md) | [繁體中文](../zh-TW/build.md) | [日本語](../ja/build.md) | [한국어](../ko/build.md) | [Français](../fr/build.md) | [Deutsch](../de/build.md) | [Español](../es/build.md) | [Italiano](build.md) | [Русский](../ru/build.md) | [العربية](../ar/build.md)

[← Indice della documentazione](README.md) · [← Progetto NeverC](project.md)

# `neverc build` / `neverc make`

NeverC include un driver **compatibile con GNU Make**. `neverc build` e
`neverc make` sono lo stesso comando: leggono il Makefile, espandono ed eseguono
le ricette. Gli [`examples/`](examples.md) seguono questo flusso.

**Non** è uno strumento di progetto `neverc.toml`. Passa opzioni Make ordinarie
e `VAR=value`.

## Sintassi

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

Opzioni: `neverc make --help`.

## Opzioni

| Opzione | Significato |
|---------|-------------|
| `-f FILE` | Leggi questo Makefile |
| `-j [N]` | Job paralleli (`-j` da solo = n. CPU) |
| `-C DIR` | Cambia directory prima della lettura |
| `-n`, `--dry-run` | Stampa senza eseguire |
| `-k`, `--keep-going` | Continua dopo errori |
| `-s`, `--silent` | Non fare eco delle ricette |
| `-B`, `--always-make` | Ricostruisci tutto |
| `-p` | Stampa DB regole/variabili |
| `VAR=VALUE` | Variabile da riga di comando |
| `-h`, `--help` | Mostra aiuto |

## Ricerca del Makefile

Senza `-f`: `GNUmakefile` → `makefile` → `Makefile`.

## Superficie Make supportata (riepilogo)

Regole e pattern, `.PHONY`, prefissi ricetta, assegnazioni, condizionali,
`include`/`export`, funzioni comuni (`subst`, `patsubst`, `wildcard`,
`foreach`, `call`, `eval`, `shell`, …). `MAKE_VERSION` riporta `4.3`.
Sottoinsieme intenzionale, non un GNU Make completo.

## Makefile tipico

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

Gli esempi di cross spesso passano `ARCH=…` o `TARGET=…`. Vedi
[Examples](examples.md).

## Comandi correlati

| Comando | Uso |
|---------|-----|
| `neverc file.c -o out` | Compilazione senza Makefile |
| [`neverc run`](run.md) | Compila ed esegue un temporaneo |
| [`neverc runtime`](runtime.md) | Installa sysroot di cross |
| [Release e `--strip`](release-builds.md) | Strip dell'immagine finale |
