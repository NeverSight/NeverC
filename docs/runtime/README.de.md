**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Dokumentationsindex](../README.de.md) · [← NeverC-Projekt](../../README.md)

# `neverc runtime`

Verwaltet **Cross-Compile-Runtime**-Pakete (Sysroots / SDKs) von
[GitHub Releases](https://github.com/NeverSight/NeverC/releases). Ablage unter
`<NeverC-root>/runtime/` (typisch `~/.neverc/runtime/`).

Lieber `neverc runtime install …` als manuelles Entpacken von
`neverc-runtime-<target>.zip`.

## Syntax

```text
neverc runtime install <target> [--version <tag>]
neverc runtime install all [--version <tag>]
neverc runtime update <target> [--version <tag>]
neverc runtime remove <target>
neverc runtime list
neverc runtime --help
```

Aliase: `upgrade` → `update`; `uninstall` → `remove`; `ls` → `list`.

## Verfügbare Targets

| Target | Layout unter `runtime/` |
|--------|-------------------------|
| `windows-x64` | `windows/x64` (+ shared `windows/shared`) |
| `windows-arm64` | `windows/arm64` (+ shared `windows/shared`) |
| `linux-x64` | `linux/x64` |
| `linux-arm64` | `linux/arm64` |
| `macos-arm64` | `macos/arm64` |
| `android-arm64` | `android/arm64` |
| `android-kernel-arm64` | `android/kernel` |

## Beispiele

```bash
neverc runtime install windows-x64
neverc runtime install all
neverc runtime update linux-arm64 --version v3389.1.2
neverc runtime remove macos-arm64
neverc runtime list
```

## Unterbefehle

- **`install`**: installiert ein Target standardmäßig mit dem **Compiler-Release-Tag** (oder `--version`). Gleicher Tag → Erfolg; anderer Tag → `[Y/n]`-Bestätigung.
- **`install all`**: installiert alle **fehlenden** Katalog-Targets; vorhandene werden übersprungen.
- **`update` / `upgrade`**: erzwungenes Holen ohne Rückfrage. Standard: **latest**.
- **`remove` / `uninstall`**: Verzeichnis löschen und `manifest.json` aktualisieren.
- **`list` / `ls`**: Installationsstatus und Compiler-Tag anzeigen.

## Versionsregeln

| Befehl | Standard ohne `--version` |
|--------|---------------------------|
| `install` / `install all` | Compiler-Release-Tag |
| `update` | Neuestes Release mit diesem Runtime-Asset |

Tags wie `vMAJOR.MINOR.PATCH`; Prüfung gegen `SHA256SUMS` vor dem Entpacken.

## Verhältnis zu `neverc update`

- `neverc runtime …` ändert nur **Sysroots**.
- [`neverc update`](../update/README.de.md) bewegt **Compiler + installierte Runtimes** gemeinsam.

Nach einem Compiler-Update sind installierte Runtimes bereits ausgerichtet; `runtime install` nur für **neue** Targets.

## Verwandte Befehle

| Befehl | Verwendung |
|--------|------------|
| [`neverc update`](../update/README.de.md) | Compiler und installierte Runtimes gemeinsam |
| [`neverc build` / `make`](../build/README.de.md) | Cross-Compile-Beispiele bauen |
| [Examples](../examples/README.de.md) | Makefiles mit `--target=…` |
| `neverc runtime --help` | Eingebaute Hilfe |
