**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Dokumentationsindex](../README.de.md) · [← NeverC-Projekt](../../README.md)

# `neverc build` / `neverc make`

NeverC liefert einen eingebetteten **GNU-Make-kompatiblen** Treiber.
`neverc build` und `neverc make` sind derselbe Befehl: Makefile lesen, expandieren,
Rezepte ausführen. Die [`examples/`](../examples/README.de.md) nutzen diesen Ablauf.

Das ist **kein** `neverc.toml`-Projektwerkzeug. Übergeben Sie normale Make-Optionen
und `VAR=value`.

## Syntax

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

Optionen: `neverc make --help`.

## Optionen

| Option | Bedeutung |
|--------|-----------|
| `-f FILE` | Diese Makefile lesen |
| `-j [N]` | Parallele Jobs (`-j` allein = CPU-Anzahl) |
| `-C DIR` | Vor dem Lesen Verzeichnis wechseln |
| `-n`, `--dry-run` | Nur anzeigen |
| `-k`, `--keep-going` | Nach Fehlern weiter |
| `-s`, `--silent` | Rezepte nicht echoen |
| `-B`, `--always-make` | Alles neu bauen |
| `-p` | Regel-/Variablen-DB drucken |
| `VAR=VALUE` | Kommandozeilenvariable |
| `-h`, `--help` | Hilfe |

## Makefile-Suche

Ohne `-f`: `GNUmakefile` → `makefile` → `Makefile`.

## Unterstützte Make-Oberfläche (Kurz)

Regeln/Musterregeln, `.PHONY`, Rezeptpräfixe, Zuweisungen, Konditionale,
`include`/`export`, gängige Funktionen (`subst`, `patsubst`, `wildcard`,
`foreach`, `call`, `eval`, `shell`, …). `MAKE_VERSION` meldet `4.3`.
Bewusst eingeschränkte Teilmenge, kein volles GNU Make.

## Typisches Makefile

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

Cross-Beispiele setzen oft `ARCH=…` oder `TARGET=…`. Siehe
[Examples](../examples/README.de.md).

## Verwandte Befehle

| Befehl | Verwendung |
|--------|------------|
| `neverc file.c -o out` | Einzeldatei ohne Makefile |
| [`neverc run`](../run/README.de.md) | Temporär kompilieren und ausführen |
| [`neverc runtime`](../runtime/README.de.md) | Cross-Sysroots installieren |
| [Release und `--strip`](../release-builds/README.de.md) | Finales Image strippen |
