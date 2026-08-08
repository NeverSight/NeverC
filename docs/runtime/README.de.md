**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Dokumentationsindex](../README.de.md) · [← NeverC-Projekt](../../README.md)

# `neverc runtime`

Verwaltet **Cross-Compile-Runtime**-Pakete (Sysroots / SDKs) von
[GitHub Releases](https://github.com/NeverSight/NeverC/releases). Ablage unter
`<NeverC-root>/runtime/` neben dem Compiler (bei Standardinstallation
`~/.neverc/runtime/`).

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

## Unterbefehle

### `install`

Installiert ein Target standardmäßig mit dem **Compiler-Release-Tag** (oder
`--version <tag>`). Asset-Name: `neverc-runtime-<target>.zip`.

```bash
neverc runtime install windows-x64
neverc runtime install linux-arm64 --version v3389.1.2
```

Wenn das Target bereits installiert ist:

- Gleicher Tag → melden und erfolgreich beenden.
- Anderer / unbekannter Tag → mit `[Y/n]` vor der Neuinstallation bestätigen.

### `install all`

Installiert alle **fehlenden** Katalog-Targets in der Compiler-Version (oder
`--version`). Bereits installierte Targets werden übersprungen; zum Ändern des
Pins ein einzelnes Target erneut mit `install` anfordern.

```bash
neverc runtime install all
```

### `update` / `upgrade`

Holt ein Target erzwungen ohne interaktive Nachfrage. Standardversion ist
**latest** (anders als `install`, das dem Compiler-Tag folgt). Mit `--version`
pinbar.

```bash
neverc runtime update windows-x64
neverc runtime update android-arm64 --version v3389.1.2
```

### `remove` / `uninstall`

Löscht das Verzeichnis eines installierten Targets und aktualisiert
`runtime/manifest.json`.

```bash
neverc runtime remove linux-x64
```

### `list` / `ls`

Zeigt jedes Katalog-Target als installiert (mit gespeichertem Tag) oder nicht
installiert sowie den aktuellen Compiler-Tag.

```bash
neverc runtime list
```

## Versionsregeln

| Befehl | Standard ohne `--version` |
|--------|---------------------------|
| `install` / `install all` | Compiler-Release-Tag |
| `update` | Neuestes Release mit diesem Runtime-Asset |

Tags sehen aus wie `vMAJOR.MINOR.PATCH`. Archive werden vor dem Entpacken gegen
`SHA256SUMS` des Releases geprüft.

## Verhältnis zu `neverc update`

- `neverc runtime …` ändert nur **Sysroots**.
- [`neverc update`](../update/README.de.md) bewegt **Compiler und alle bereits
  installierten Runtimes** in einer Transaktion auf einen Tag.

Nach einem Compiler-Upgrade mit `neverc update` sind installierte Runtimes
bereits ausgerichtet; `runtime install` brauchen Sie nur für **neue** Targets.

## Verwandte Befehle

| Befehl | Verwendung |
|--------|------------|
| [`neverc update`](../update/README.de.md) | Compiler und installierte Runtimes gemeinsam up-/downgraden |
| [`neverc build` / `make`](../build/README.de.md) | Cross-Compile-Beispiele gegen diese Sysroots bauen |
| [Examples](../examples/README.de.md) | Beispiel-`Makefile`s mit `--target=…` |
| `neverc runtime --help` | Eingebaute Kurzübersicht |
