**Lingue**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Indice della documentazione](../README.it.md) · [← Progetto NeverC](../../README.md)

# `neverc runtime`

Gestisce i pacchetti di **runtime per cross-compilation** (sysroot / SDK) da
[GitHub Releases](https://github.com/NeverSight/NeverC/releases). Vivono sotto
`<NeverC-root>/runtime/` accanto al compilatore (installazione predefinita:
`~/.neverc/runtime/`).

Preferisci `neverc runtime install …` all’estrazione manuale di
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
| `windows-x64` | `windows/x64` (+ condiviso `windows/shared`) |
| `windows-arm64` | `windows/arm64` (+ condiviso `windows/shared`) |
| `linux-x64` | `linux/x64` |
| `linux-arm64` | `linux/arm64` |
| `macos-arm64` | `macos/arm64` |
| `android-arm64` | `android/arm64` |
| `android-kernel-arm64` | `android/kernel` |

## Sottocomandi

### `install`

Installa un target con il **tag di release del compilatore** per impostazione
predefinita (o `--version <tag>`). Nome dell’asset: `neverc-runtime-<target>.zip`.

```bash
neverc runtime install windows-x64
neverc runtime install linux-arm64 --version v3389.1.2
```

Se il target è già installato:

- Stesso tag → segnala ed esce con successo.
- Tag diverso / sconosciuto → conferma `[Y/n]` prima di reinstallare.

### `install all`

Installa **tutti i target mancanti** del catalogo alla versione del compilatore
(o `--version`). Quelli già installati vengono saltati; per cambiare il pin,
riesegui `install` su un singolo target.

```bash
neverc runtime install all
```

### `update` / `upgrade`

Forza il download di un target senza prompt interattivo. La versione predefinita
è **latest** (a differenza di `install`, che segue il tag del compilatore).
Passa `--version` per fissarla.

```bash
neverc runtime update windows-x64
neverc runtime update android-arm64 --version v3389.1.2
```

### `remove` / `uninstall`

Elimina la directory di un target installato e aggiorna
`runtime/manifest.json`.

```bash
neverc runtime remove linux-x64
```

### `list` / `ls`

Mostra ogni target del catalogo come installato (con tag registrato) o non
installato, più il tag live del compilatore.

```bash
neverc runtime list
```

## Regole di versione

| Comando | Predefinito senza `--version` |
|---------|-------------------------------|
| `install` / `install all` | Tag di release del compilatore |
| `update` | Ultima release che pubblica quell’asset runtime |

I tag hanno forma `vMAJOR.MINOR.PATCH`. Gli archivi sono verificati rispetto a
`SHA256SUMS` della release prima dell’estrazione.

## Relazione con `neverc update`

- `neverc runtime …` modifica **solo i sysroot**.
- [`neverc update`](../update/README.it.md) sposta **compilatore e tutti i
  runtime già installati** a un tag in un’unica transazione.

Dopo un upgrade del compilatore con `neverc update`, i runtime installati sono
già allineati; serve `runtime install` solo per i target **nuovi**.

## Comandi correlati

| Comando | Quando usarlo |
|---------|---------------|
| [`neverc update`](../update/README.it.md) | Aggiornare/declassare insieme compilatore + runtime installati |
| [`neverc build` / `make`](../build/README.it.md) | Compilare esempi cross contro questi sysroot |
| [Examples](../examples/README.it.md) | `Makefile` di esempio con `--target=…` |
| `neverc runtime --help` | Riepilogo d’uso integrato |
