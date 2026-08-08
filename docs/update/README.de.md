**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Dokumentationsindex](../README.de.md) · [← NeverC-Projekt](../../README.md)

# `neverc update`

Aktualisiert eine **Release-Installation**, sodass Compiler und alle bereits
installierten Cross-Compile-Runtimes gemeinsam auf **einen konkreten Release-Tag**
wechseln. `neverc upgrade` ist ein Alias.

Für Installationen über `install.sh` (typisch `~/.neverc`). Aktualisiert **kein**
CMake/Ninja-Quellbaum — PATH wechseln und neu bauen; siehe
[Lokale Entwicklung](../local-dev/README.de.md).

## Syntax

```text
neverc update
neverc update <version>
neverc update --version <version>
neverc update --help
```

Beispiele:

```bash
neverc update                 # neuestes vollständiges Release für diesen Host
neverc update v3389.1.2       # exakter Tag (Up- oder Downgrade)
neverc update 3389.1.2        # führendes „v“ optional
neverc upgrade                # gleich wie neverc update
```

`-y` / `--yes` werden für Skripte akzeptiert; Updates sind nicht interaktiv.

## Synchronisationsumfang

| Komponente | Verhalten |
|------------|-----------|
| Compiler (`bin/`, `lib/`, `pluginsdk/`) | Ersetzt, wenn der Ziel-Tag abweicht |
| Installierte Runtimes unter `runtime/` | Nur **bereits installierte** Targets werden neu geholt und gepinnt |
| Fehlende Runtimes | Werden **nicht** automatisch installiert — [`neverc runtime install`](../runtime/README.de.md) |

## Sicherheitsmodell

1. Exklusives Lock unter `<install>/.neverc-update.lock`.
2. Ziel-Tag auflösen.
3. `SHA256SUMS` und Archive laden und prüfen.
4. Staging, Validierung, Commit; bei Fehler Rollback.

Bei einem schlechten Runtime-Release auf einen älteren Tag zurück:

```bash
neverc update v3389.0.1
```

## Einschränkungen

- Nur Release-Installationswurzel (meist `~/.neverc`). Dateisystemwurzeln und CMake-Buildbäume werden abgelehnt.
- Host muss zu einem veröffentlichten Compiler-Asset passen.
- Unter Windows kann ein kurzer Helper `neverc.exe` nach dem Beenden ersetzen.

## Verwandte Befehle

| Befehl | Verwendung |
|--------|------------|
| [`neverc runtime`](../runtime/README.de.md) | Einzelne Sysroots ohne Compiler-Wechsel |
| [`neverc run`](../run/README.de.md) | Temporäres Host-Binary kompilieren und ausführen |
| [`neverc build` / `make`](../build/README.de.md) | Beispiel-/Projekt-Makefiles steuern |
| `neverc update --help` | Eingebaute Hilfe |
