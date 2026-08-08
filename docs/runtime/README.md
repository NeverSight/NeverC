**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Documentation index](../README.md) · [← NeverC project](../../README.md)

# `neverc runtime`

Manage **cross-compilation runtime** packages (sysroots / SDKs) downloaded from
[GitHub Releases](https://github.com/NeverSight/NeverC/releases). Packages live
under `<NeverC-root>/runtime/` next to the compiler (for a default install,
`~/.neverc/runtime/`).

Prefer `neverc runtime install …` over manually unpacking
`neverc-runtime-<target>.zip` archives.

## Syntax

```text
neverc runtime install <target> [--version <tag>]
neverc runtime install all [--version <tag>]
neverc runtime update <target> [--version <tag>]
neverc runtime remove <target>
neverc runtime list
neverc runtime --help
```

Aliases: `upgrade` → `update`; `uninstall` → `remove`; `ls` → `list`.

## Available targets

| Target | Contents layout (under `runtime/`) |
|--------|-------------------------------------|
| `windows-x64` | `windows/x64` (+ shared `windows/shared`) |
| `windows-arm64` | `windows/arm64` (+ shared `windows/shared`) |
| `linux-x64` | `linux/x64` |
| `linux-arm64` | `linux/arm64` |
| `macos-arm64` | `macos/arm64` |
| `android-arm64` | `android/arm64` |
| `android-kernel-arm64` | `android/kernel` |

## Subcommands

### `install`

Install one target at the **compiler’s release tag** by default (or
`--version <tag>`). Asset name: `neverc-runtime-<target>.zip`.

```bash
neverc runtime install windows-x64
neverc runtime install linux-arm64 --version v3389.1.2
```

If the target is already installed:

- Same tag → report and exit successfully.
- Different / unknown tag → prompt `[Y/n]` before reinstalling.

### `install all`

Install **every missing** catalog target at the compiler version (or
`--version`). Already-installed targets are skipped; reinstall a single target
explicitly if you need to change its pin.

```bash
neverc runtime install all
```

### `update` / `upgrade`

Force-fetch one target without an interactive prompt. Default version is
**latest** (unlike `install`, which defaults to the compiler tag). Pass
`--version` to pin.

```bash
neverc runtime update windows-x64
neverc runtime update android-arm64 --version v3389.1.2
```

### `remove` / `uninstall`

Delete an installed target directory and update `runtime/manifest.json`.

```bash
neverc runtime remove linux-x64
```

### `list` / `ls`

Show each catalog target as installed (with recorded tag) or not installed, plus
the live compiler tag.

```bash
neverc runtime list
```

## Version rules

| Command | Default when `--version` omitted |
|---------|----------------------------------|
| `install` / `install all` | Compiler release tag |
| `update` | Latest release that publishes that runtime asset |

Tags look like `vMAJOR.MINOR.PATCH`. Archives are SHA256-checked against the
release `SHA256SUMS` before extraction.

## Relationship to `neverc update`

- `neverc runtime …` changes **sysroots only**.
- [`neverc update`](../update/README.md) moves the **compiler and all already
  installed runtimes** to one tag as a single transaction.

After upgrading the compiler with `neverc update`, installed runtimes are
already aligned; you only need `runtime install` for **new** targets.

## Related commands

| Command | When to use it |
|---------|----------------|
| [`neverc update`](../update/README.md) | Upgrade/downgrade compiler + installed runtimes together |
| [`neverc build` / `make`](../build/README.md) | Build examples that cross-compile against these sysroots |
| [Examples](../examples/README.md) | Sample `Makefile`s that invoke `neverc` with `--target=…` |
| `neverc runtime --help` | Built-in usage summary |
