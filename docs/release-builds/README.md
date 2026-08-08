**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Documentation index](../README.md) · [← NeverC project](../../README.md)

# Release binaries and `--strip`

Use `--strip` when producing an executable, shared library, or final Android
kernel module for distribution. Its short alias is `-s`; both spellings have
identical behavior.

## Quick start

```bash
neverc -O2 --strip app.c -o app
neverc -O2 -s app.c -o app

cd examples/android-kernel-hello
neverc make release
```

NeverC performs stripping inside its integrated linker. It does not launch an
external `llvm-strip`, so the same command works for cross-target ELF, Mach-O,
and PE/COFF output.

Do not confuse this CLI option with the CMake packaging switch
`NEVERC_STRIP_BINARY`: that switch post-processes only the `neverc` compiler
executable and may invoke an external strip tool. It does not affect programs
compiled by NeverC.

## Debug and symbol policy

| Invocation | Source-level debug information | Ordinary static symbol names | Darwin `.dSYM` |
|------------|--------------------------------|------------------------------|----------------|
| Default (no `-g`) | Not generated | May remain; exact defaults are format-dependent | Not generated |
| `-g` | Generated | Remain | Generated for a normal Darwin link |
| `--strip` | Removed if present | Non-runtime names are removed | Not generated |
| `-g --strip` | Strip policy wins; absent from the delivered image | Non-runtime names are removed | Suppressed |

Without `-g`, the frontend starts with no source-level debug information. That
does **not** mean the output is fully stripped: ELF and Mach-O can still carry
ordinary symbol names, while PE normally has no static COFF symbol table unless
debug settings request one. Auto-LTO may discard some local names, but it is not
a strip-all guarantee.

`-g` changes the policy from no source debug to source-level debug; it is not
“more debug” layered on top of debug emitted by default. Unwind metadata such as
ELF/Mach-O `.eh_frame` or PE `.pdata`/`.xdata` is runtime metadata, not
source-level DWARF, and can remain in a stripped image.

## Implementation and format behavior

The driver converts `--strip` into one typed linker policy and passes it to all
three backends. Each backend applies the policy while it still understands the
format, preserving names and records that the loader or dynamic ABI requires.

| Format | Removed | Preserved when required |
|--------|---------|-------------------------|
| ELF | `.debug*` data and the ordinary static symbol/string tables | Dynamic imports/exports, relocation and loader metadata, unwind information |
| Android kernel `.ko` (ELF ET_REL) | `.debug*`, `.comment`, and local/undefined symbols not required by retained relocations | One `.symtab` linked to `.strtab`, all relocations and their targets, defined global symbols, imports, `__versions`, `.codetag.alloc_tags`, module ABI data |
| Mach-O | Debug maps/STABS, non-runtime local and global symbol entries, and companion `.dSYM` generation | Binding/import data, exported ABI names, export trie entries, runtime-referenced symbols |
| PE/COFF | Embedded DWARF sections and the static COFF symbol/string table when present | PE imports/exports, unwind tables, load configuration and other loader metadata |

## Scope and precedence

- `--strip` supports final linked executables, shared libraries, and the narrow
  final Android `.ko` exception described below.
- NeverC rejects it with `-c`, ordinary `-r`, Android intermediate `.o` output,
  `--emit-static-lib`, or `-fdyncode` instead of silently producing an
  unstripped non-final artifact.
- Strip policy wins over `-g` and backend debug switches.
- It is covered for both NeverC's default Auto-LTO pipeline and `-fno-lto`.
- Shared-library import and export names remain whenever removing them would
  break the dynamic ABI.

## Android kernel modules

An Android module is a final deliverable but remains ELF `ET_REL`; the Linux
module loader rejects a strip-all result because it needs a symbol table,
linked string table, undefined imports, and relocations. NeverC therefore
accepts `--strip` with `-r` only when all of these final-module conditions hold:

- the target is Android;
- `-fandroid-kernel-driver-mode` and `-r` are active;
- the output name ends in `.ko`.

On that path, `--strip` models the safe boundary of
`llvm-strip --strip-unneeded`, not `--strip-all`: it removes debug sections,
`.comment`, and local or undefined symbols unused by retained relocations, then
rebuilds `.strtab` so removed names do not survive as stale bytes. It preserves
exactly one `.symtab` linked to `.strtab`, all relocations and required targets,
defined non-local symbols, imports, `__versions`, `.codetag.alloc_tags`,
`.gnu.linkonce.this_module`, and other module-loader metadata.

Do not post-process a `.ko` with `llvm-strip --strip-all`. Do not blindly remove
`.codetag.alloc_tags` or `__codetag_*`; these can be loader/runtime ABI data.
If module signing is used, strip first and sign the final bytes because any
post-signing mutation invalidates the signature. A `clean` target must only
delete files—it must never strip or sign an existing module.

## Security boundary

Stripping removes high-value naming and debug metadata, which raises the cost
of analysis, but it is **not** obfuscation and cannot make native machine code
impossible to reverse engineer. A correct stripped binary may still contain:

- dynamic import and export names required by the loader;
- symbol names required by retained `.ko` relocations;
- string literals, reflection tables, or application-defined metadata;
- unwind, relocation, signing, and load-configuration records;
- the machine code and its observable control flow.

`--strip` only governs the final image. It does not delete separately requested
artifacts such as link maps, optimization records, or `-save-temps` outputs;
audit the release directory and do not distribute those side files.

Use string encryption, obfuscation, and anti-tamper measures as separate layers
when appropriate, and never embed secrets that must remain confidential in a
client binary.

## Verifying an artifact

Inspect release artifacts in CI with LLVM's object tools. Adjust commands for
the target format and explicitly allow the ABI names your program needs.

```bash
llvm-readobj --sections --symbols --dyn-symbols app
llvm-dwarfdump app
strings app | grep neverc_private_release_symbol
test ! -e app.dSYM

llvm-readelf -h -S -s -r examples/android-kernel-hello/nvk_hello.ko
llvm-dwarfdump examples/android-kernel-hello/nvk_hello.ko
```

A stripped artifact should have no source-level debug sections or private
static symbol names. Required dynamic names and runtime metadata are expected
and should not be treated as a stripping failure.
