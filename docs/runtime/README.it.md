**Lingue**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Indice della documentazione](../README.it.md) · [← Progetto NeverC](../../README.md)

# `neverc runtime`

Gestisce i pacchetti **runtime di cross-compilazione** (sysroot / SDK) da
[GitHub Releases](https://github.com/NeverSight/NeverC/releases). Vivono in
`<NeverC-root>/runtime/` (di solito `~/.neverc/runtime/`).

Preferisci `neverc runtime install …` all'estrazione manuale di
`neverc-runtime-<target>.zip`.

## Sintassi

```text
neverc runtime install <target> [--version <tag>]
neverc runtime install all [--version <tag>]
neverc runtime update <target> [--version <tag>]
neverc runtime remove <target>
neverc runtime list
neverc runtime --help
```

Alias: `upgrade` → `update`; `uninstall` → `remove`; `ls` → `list`.

## Target disponibili

| Target | Layout sotto `runtime/` |
|--------|-------------------------|
| `windows-x64` | `windows/x64` (+ shared `windows/shared`) |
| `windows-arm64` | `windows/arm64` (+ shared `windows/shared`) |
| `linux-x64` | `linux/x64` |
| `linux-arm64` | `linux/arm64` |
| `macos-arm64` | `macos/arm64` |
| `android-arm64` | `android/arm64` |
| `android-kernel-arm64` | `android/kernel` |

## Esempi

```bash
neverc runtime install windows-x64
neverc runtime install all
neverc runtime update linux-arm64 --version v3389.1.2
neverc runtime remove macos-arm64
neverc runtime list
```

## Sottocomandi

- **`install`**: installa un target con il **tag del compilatore** di default (o `--version`). Stesso tag → successo; diverso → conferma `[Y/n]`.
- **`install all`**: installa tutti i target **mancanti**; quelli già presenti sono saltati.
- **`update` / `upgrade`**: forza il download senza prompt. Default: **latest**.
- **`remove` / `uninstall`**: rimuove la directory e aggiorna `manifest.json`.
- **`list` / `ls`**: stato di installazione e tag del compilatore.

## Regole di versione

| Comando | Default senza `--version` |
|---------|---------------------------|
| `install` / `install all` | Tag release del compilatore |
| `update` | Ultima release che pubblica quell'asset runtime |

Tag `vMAJOR.MINOR.PATCH`; verifica con `SHA256SUMS` prima dell'estrazione.

## Relazione con `neverc update`

- `neverc runtime …` modifica solo i **sysroot**.
- [`neverc update`](../update/README.it.md) allinea **compilatore + runtime già installati**.

Dopo l'aggiornamento del compilatore, usa `runtime install` solo per target **nuovi**.

## Comandi correlati

| Comando | Uso |
|---------|-----|
| [`neverc update`](../update/README.it.md) | Compilatore e runtime installati insieme |
| [`neverc build` / `make`](../build/README.it.md) | Esempi di cross-compilazione |
| [Examples](../examples/README.it.md) | Makefile con `--target=…` |
| `neverc runtime --help` | Guida incorporata |
