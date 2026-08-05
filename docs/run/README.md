**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Documentation index](../README.md) · [← NeverC project](../../README.md)

# `neverc run`

Compile a C or NeverC program to a **temporary executable**, execute it on the
**local host**, return its exit status, and remove the artifact afterward. The
workflow is intentionally similar to `go run`.

Use the normal compiler invocation (`neverc ... -o output`) when you need to keep
the binary, ship it, or debug it with a debugger.

## Syntax

```text
neverc run [compiler flags] file.c [file2.nc ...] [program arguments...]
neverc run [compiler arguments...] -- [program arguments...]
```

Run `neverc run --help` for a built-in summary.

## Argument parsing

`neverc run` splits arguments into a **compiler invocation** and optional
**program arguments** using one of two rules.

### Default (Go-style) split

1. Scan left to right for the first argument whose name ends in `.c` or `.nc` and
   does not start with `-`.
2. Everything **before and including** the consecutive run of `.c`/`.nc` files
   is passed to the compiler.
3. Everything **after** that run is passed to `argv` of the temporary program.

Examples:

```bash
# Compiler: -O2 -fbuiltin-string hello.c
# Program:  (none)
neverc run -O2 -fbuiltin-string hello.c

# Compiler: -O2 main.c helper.nc
# Program:  --verbose two words
neverc run -O2 main.c helper.nc -- --verbose two words

# Compiler: -DGENERATED=.c -O2 main.c
# Program:  argument
neverc run -DGENERATED=.c -O2 main.c argument
```

Notes:

- Only `.c` and `.nc` extensions are treated as run sources. A flag such as
  `-DGENERATED=.c` stays on the compiler side because it starts with `-`.
- Multiple sources compile into one temporary binary, like a normal multi-file
  link.

### Explicit `--` separator

When the compiler needs arguments **after** the source list (linker flags,
non-source inputs, `-x c -`, and similar), put `--` between the compiler tail
and the program arguments:

```bash
# Compiler: hello.c helper.o -lm
# Program:  arg.c -x        (these are argv, not compiler flags)
neverc run hello.c helper.o -lm -- arg.c -x

# Compiler: hello.c -O1
# Program:  x
neverc run hello.c -O1 -- x
```

Everything before `--` is forwarded verbatim to `neverc` (plus an internal
`-o <temp>`). Everything after `--` becomes program arguments.

## What happens at runtime

| Topic | Behavior |
|-------|----------|
| Working directory | The temporary program runs in your **current directory**. Relative paths behave the same as with a normal binary you built in that directory. |
| Environment | The process inherits the current environment (`PATH`, exported variables, and so on). |
| Standard I/O | stdin, stdout, and stderr are connected to the temporary process — pipes and redirects work as usual. |
| Exit status | On success, `neverc run` returns the **program's** exit code. If compilation fails, it returns the **compiler's** exit code and **does not** execute the program. |
| Temporary files | The executable lives under a unique `neverc-run-*` directory. The directory is removed after the run finishes (success or program failure). Cleanup failure is reported separately. |

## Examples

**Quick run with optimizations and the string builtin:**

```bash
neverc run -O2 -fbuiltin-string hello.c
```

**Pass arguments to `main` (including words with spaces):**

```bash
neverc run -fbuiltin-string greet.c -- Alice "two words"
```

**Compile several translation units, then run:**

```bash
neverc run -O2 main.c util.nc -- --port 8080
```

**Use `--` when compiler arguments follow the sources:**

```bash
neverc run app.c extra.o -lm -- --config prod.json
```

## Limitations and caveats

- **Host execution only.** `neverc run` always tries to execute the temporary
  binary on the machine where you invoked `neverc`. Cross-compilation flags
  (`-target ...`) may still compile, but the result is usually not runnable
  locally.
- **No persistent artifact.** You cannot attach a debugger to the output after
  the command completes because the binary is deleted. Use `neverc ... -o out`
  when you need a durable executable.
- **Same toolchain as `neverc`.** The command re-invokes the same `neverc`
  binary that handled `run`, forwarding your compiler flags unchanged (aside
  from `-o`).
- **`.nc` sources.** Native NeverC files use the same rules as `.c`; language
  extensions enabled for `.nc` apply automatically.

## Related commands

| Command | When to use it |
|---------|----------------|
| `neverc file.c -o out` | Keep the binary, cross-compile, or integrate with build scripts |
| `neverc build` / `neverc make` | Project-style builds with a `neverc.toml` |
| `neverc run --help` | Built-in usage summary |
