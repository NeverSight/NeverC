**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Dokumentationsindex](../README.de.md) · [← NeverC-Projekt](../../README.md)

# `neverc run`

Kompiliert ein C- oder NeverC-Programm in eine **temporäre ausführbare Datei**, führt es auf dem **lokalen Host** aus, gibt den Exit-Status zurück und löscht das Artefakt danach. Der Ablauf ist absichtlich an `go run` angelehnt.

Wenn du das Binary behalten, verteilen oder mit einem Debugger untersuchen willst, verwende den normalen Compileraufruf (`neverc ... -o output`).

## Syntax

```text
neverc run [Compiler-Flags] file.c [file2.nc ...] [Programmargumente...]
neverc run [Compiler-Argumente...] -- [Programmargumente...]
```

`neverc run --help` zeigt eine eingebaute Kurzübersicht.

## Argumentaufteilung

`neverc run` teilt Argumente mit einer von zwei Regeln in einen **Compileraufruf** und optionale **Programmargumente** auf.

### Standard (Go-Stil)

1. Von links nach rechts das erste Argument finden, das mit `.c` oder `.nc` endet und nicht mit `-` beginnt.
2. **Alles bis einschließlich der aufeinanderfolgenden `.c`/`.nc`-Dateien** (plus alle Argumente davor) geht an den Compiler.
3. **Alles danach** geht an `argv` des temporären Programms.

Beispiele:

```bash
neverc run -O2 -fbuiltin-string hello.c
neverc run -O2 main.c helper.nc -- --verbose two words
neverc run -DGENERATED=.c -O2 main.c argument
```

Hinweise:

- Nur `.c` und `.nc` gelten als Run-Quellen. Flags wie `-DGENERATED=.c` bleiben beim Compiler.
- Mehrere Quellen werden wie bei einem normalen Multi-File-Link zu einem temporären Binary gebaut.

### Explizites `--`

Wenn der Compiler Argumente **nach** der Quellliste braucht (Linker-Flags, Nicht-Quell-Inputs, `-x c -` usw.), trenne Compiler und Programm mit `--`:

```bash
neverc run hello.c helper.o -lm -- arg.c -x
neverc run hello.c -O1 -- x
```

Alles vor `--` wird an `neverc` weitergegeben (plus internes `-o <temp>`). Alles danach sind Programmargumente.

## Laufzeitverhalten

| Thema | Verhalten |
|-------|-----------|
| Arbeitsverzeichnis | Das temporäre Programm läuft im **aktuellen Verzeichnis** |
| Umgebung | Erbt die aktuelle Umgebung (`PATH`, exportierte Variablen usw.) |
| Standard-I/O | stdin/stdout/stderr sind mit dem temporären Prozess verbunden |
| Exit-Status | Bei Erfolg der **Programm**-Exit-Code; bei Compilerfehler der **Compiler**-Exit-Code, ohne Programmstart |
| Temporäre Dateien | Die EXE liegt in `neverc-run-*`; das Verzeichnis wird danach entfernt. Fehlgeschlagene Bereinigung wird separat gemeldet. |

## Beispiele

```bash
neverc run -O2 -fbuiltin-string hello.c
neverc run -fbuiltin-string greet.c -- Alice "two words"
neverc run -O2 main.c util.nc -- --port 8080
neverc run app.c extra.o -lm -- --config prod.json
```

## Grenzen und Hinweise

- **Nur Host-Ausführung.** Cross-Compile-Flags (`-target ...`) können kompilieren, aber das temporäre Binary wird immer lokal ausgeführt.
- **Kein persistentes Artefakt.** Nach Abschluss ist das Binary weg — für Debugging `neverc ... -o out` verwenden.
- **Gleiche Toolchain wie `neverc`.** Es wird dieselbe `neverc`-Binary erneut aufgerufen.
- **`.nc`-Quellen.** Gleiche Regeln wie `.c`; Spracherweiterungen für `.nc` gelten automatisch.

## Verwandte Befehle

| Befehl | Wann verwenden |
|--------|----------------|
| `neverc file.c -o out` | Binary behalten, Cross-Compile, Build-Skripte |
| `neverc build` / `neverc make` | Projektbuilds mit `neverc.toml` |
| `neverc run --help` | Eingebaute Kurzübersicht |
