**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Documentation index](../README.md) · [← NeverC project](../../README.md)

# `neverc update`

Update a **release installation** of NeverC so the compiler and every already
installed cross-compilation runtime move to **one concrete release tag**
together. `neverc upgrade` is an alias with the same behavior.

Use this after `install.sh` (or a package install under `~/.neverc`). It does
**not** update a CMake/Ninja source build tree — switch those with PATH and a
rebuild; see [Local Development](../local-dev/README.md).

## Syntax

```text
neverc update
neverc update <version>
neverc update --version <version>
neverc update --help
```

Examples:

```bash
neverc update                 # newest complete release for this host
neverc update v3389.1.2       # exact tag (upgrade or downgrade)
neverc update 3389.1.2        # leading "v" is optional
neverc upgrade                # same as neverc update
```

`-y` / `--yes` are accepted for scripting symmetry; updates are non-interactive.

## What is synchronized

| Component | Behavior |
|-----------|----------|
| Compiler (`bin/`, `lib/`, `pluginsdk/`) | Replaced when the target tag differs from the live compiler |
| Installed runtimes under `runtime/` | Only targets that are **already installed** are re-fetched and pinned to the same tag |
| Missing runtimes | **Not** installed automatically — use [`neverc runtime install`](../runtime/README.md) |

Windows release archives may bundle a `runtime/` tree; the updater still manages
compiler roots and previously installed runtime packages as separate units.

## Safety model

1. Acquire an exclusive lock under `<install>/.neverc-update.lock`.
2. Resolve the target tag (latest host compiler asset, or the exact tag you named).
3. Download `SHA256SUMS` and every required archive; verify checksums.
4. Extract and validate into a staging directory (compiler `-dumpversion` must
   match the tag; runtime directories and `manifest.json` must be present).
5. Commit into the live install; on failure, roll back. Staging or checksum
   failures leave the current install untouched.

If a runtime release is bad, point at an older tag:

```bash
neverc update v3389.0.1
```

That downgrades the compiler and all installed runtimes together.

## Host and install constraints

- Works only from a release install root (typically `~/.neverc`). The tool
  refuses filesystem roots and directories that look like CMake build trees
  (`CMakeCache.txt` plus `build.ninja` or `Makefile`).
- The host platform must match a published compiler asset (same distribution
  matrix as the installer).
- On Windows, applying the commit may use a short-lived helper process so the
  running `neverc.exe` can be replaced after exit.

## Related commands

| Command | When to use it |
|---------|----------------|
| [`neverc runtime`](../runtime/README.md) | Install, list, or remove individual sysroots without changing the compiler |
| [`neverc run`](../run/README.md) | Compile-and-run a temporary host binary |
| [`neverc build` / `make`](../build/README.md) | Drive example or project Makefiles |
| `neverc update --help` | Built-in usage summary |
