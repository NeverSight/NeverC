**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Documentation index](../README.md) · [← NeverC project](../../README.md)

# `neverc build` / `neverc make`

NeverC ships an embedded **GNU Make–compatible** driver. `neverc build` and
`neverc make` are the same command: they read a Makefile, expand variables and
functions, and run recipes. Example projects under [`examples/`](../examples/README.md)
are written for this workflow.

This is **not** a `neverc.toml` project tool. Pass ordinary Make flags and
`VAR=value` overrides on the command line.

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

Run `neverc make --help` for the built-in option list.

## Options

| Option | Meaning |
|--------|---------|
| `-f FILE` | Read `FILE` instead of the default makefile |
| `-j [N]` | Parallel jobs (`-j` alone uses the host CPU count) |
| `-C DIR` | `chdir` before reading the makefile (repeatable) |
| `-n`, `--dry-run` | Print recipes without executing |
| `-k`, `--keep-going` | Continue after recipe errors |
| `-s`, `--silent` | Do not echo recipes |
| `-B`, `--always-make` | Rebuild all targets unconditionally |
| `-p` | Print the rule / variable database and exit |
| `VAR=VALUE` | Command-line variable (overrides the makefile unless `override`) |
| `-h`, `--help` | Show usage |

## Makefile discovery

When `-f` is omitted, NeverC searches the current directory in order:

1. `GNUmakefile`
2. `makefile`
3. `Makefile`

## Supported Make surface (summary)

Enough of GNU Make for NeverC’s examples and typical small projects:

- Rules, pattern rules, `.PHONY`, recipe prefixes `@`, `-`, `+`
- Assignments: `=`, `:=` / `::=`, `+=`, `?=`, `!=` (shell assign), `override`, `define`/`endef`
- Conditionals: `ifeq` / `ifneq` / `ifdef` / `ifndef`, including `else ifeq …`
- `include` / `-include` / `sinclude`, `export` / `unexport`, `undefine`
- Common functions: `subst`, `patsubst`, `filter`, `wildcard`, `foreach`, `call`,
  `eval`, `shell`, `error` / `warning` / `info`, path helpers (`dir`, `notdir`,
  `abspath`, …), and related string utilities
- Automatic / built-in variables such as `CURDIR`, `MAKE`, `MAKEFLAGS`,
  `MAKECMDGOALS`, `MAKE_VERSION` (reports `4.3` for compatibility)

It is intentionally a focused Make subset, not a drop-in replacement for every
GNU Make extension or every third-party Makefile in the wild.

## Typical NeverC Makefile pattern

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

Cross-compilation examples often pass `ARCH=…` or `TARGET=…` on the command
line; Android samples commonly define a `run` target that uses `adb`. Details:
[Examples](../examples/README.md).

## Related commands

| Command | When to use it |
|---------|----------------|
| `neverc file.c -o out` | Single-file or scripted compile without a Makefile |
| [`neverc run`](../run/README.md) | Temporary compile-and-run on the host |
| [`neverc runtime`](../runtime/README.md) | Install sysroots needed by cross `--target` builds |
| [Release binaries and `--strip`](../release-builds/README.md) | Strip final linked images for distribution |
