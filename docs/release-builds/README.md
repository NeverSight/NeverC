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
| Android kernel `.ko` (ELF ET_REL) | `.debug*`, `.comment`, relocation-unneeded local/undefined entries, and readable names of ordinary retained definitions | One `.symtab` linked to `.strtab`, all relocations and targets, exact loader/CFI names, exact imports, protected-section names, and module ABI metadata |
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

`neverc make release` remains the recommended release command and expands to
`-O2 --strip`. With no `.nvk-build-flags` stamp, `make` defaults to debug and
does not select release on its own. The example Makefiles save an explicit
profile so later `make push`, `make run`, and bare `make` calls keep using the
same artifact. `make debug` or an explicit `PROFILE=...` replaces the saved
selection; `make clean` removes the stamp, so the next build defaults to debug.
On this final-module path, NeverC removes debug sections, `.comment`, and
relocation-unneeded local/undefined entries, then rebuilds `.strtab`.

Eligible retained definitions receive deterministic IDA-inspired,
non-reserved structural names:

- `STT_FUNC` becomes `fn_HEX`;
- `STT_OBJECT` becomes `obj_HEX`;
- executable `STT_NOTYPE` becomes `code_HEX`;
- other allocated `STT_NOTYPE` becomes `sym_HEX`;
- `SHN_ABS` becomes `abs_HEX`;
- a definition outside `SHF_ALLOC` becomes
  `sym_S<FINAL_SECTION_ORDINAL_HEX>_<OFFSET_HEX>`.

Every `HEX` field, including both fields in the non-allocated form, uses
uppercase hexadecimal without redundant leading zeroes. If several symbols
need the same spelling, deterministic decimal aliases `_1`, `_2`, and so on are
appended.

These spellings are inspired by IDA without occupying its dummy-name
namespace. In a fresh IDA 9.4 database, stored ELF user symbols `sub_0`,
`sub_4`, and `loc_8` display as `_sub_0`, `_sub_4`, and `_loc_8`, whereas
`fn_0`, `code_8`, and `obj_10` display unchanged. Hex-Rays also documents that
[`SN_NODUMMY`](https://python.docs.hex-rays.com/ida_name/index.html) prepends an
underscore to a user name beginning with a dummy prefix such as `sub_`.
NeverC does not deliberately clear an ordinary definition's `st_name` to make
IDA synthesize `sub_`: Android/Linux module kallsyms has historically ignored
zero-name entries, and empty names would remove the auditable serialized naming
contract. Entries that are already required to be empty and section symbols
still remain exact.

ELF permits several symbols to share one canonical analysis EA. NeverC
preserves or generates the complete alias set in `.symtab`; however, IDA 9.4's
address-name model may materialize only one primary name among symbols at that
address. An alias absent from IDA's display has therefore not necessarily been
lost from the ELF; audit the complete set with `llvm-readelf` or `llvm-nm`.

For an allocated symbol, `HEX` is NeverC's canonical analysis EA: the canonical
effective address used only for static analysis. Starting with a cursor of zero,
NeverC visits final retained `SHF_ALLOC` sections in final section-header order,
aligns the cursor to `max(sh_addralign, 1)`, records that section's base, and
advances by `max(sh_size, 1)`; the EA is that base plus final `st_value`.
`abs_HEX` uses the absolute final `st_value`. In the non-allocated form,
`FINAL_SECTION_ORDINAL_HEX` is the final section ordinal and `OFFSET_HEX` is the
final `st_value` within that section. These coordinates are not a hash,
encryption, file offset, ELF virtual address, or kernel runtime address. The
loader and KASLR may place the module elsewhere at runtime.

The following names remain exact:

- every `SHN_UNDEF` import, because the module loader resolves it by name;
- symbols defined in `.modinfo`, `.text.ftrace_trampoline`,
  `.gnu.linkonce.this_module`, `__versions`, or `.codetag.alloc_tags`;
- `init_module`, `cleanup_module`, `__cfi_check`, `__cfi_check_fail`,
  `__cfi_jt_init_module`, and `__cfi_jt_cleanup_module`;
- names beginning with `__typeid__` or `__kcfi_typeid_`.

IDA's `extern` area is a synthetic analysis view, not an ELF section. In a
final `ET_REL` `.ko`, external relocation targets are `SHN_UNDEF` entries in
`.symtab`, whose exact names the loader needs. The policy therefore follows
the real ELF symbol class and defining section: undefined imports remain exact,
while eligible definitions are renamed regardless of how a tool groups them.

Names are planned globally before mutation. Definitions that share a base
candidate receive the unsuffixed name, then `_1`, `_2`, and so on in
deterministic order; this ordinary alias case is not an error. Finalization
aborts if a generated name collides with the exact-name reserved namespace, or
if coordinate or suffix arithmetic overflows. It also fails closed instead of
guessing when it encounters `SHN_COMMON`, `SHN_LIVEPATCH`, or an unknown
reserved ELF section index. `SHN_COMMON` is not valid in a loadable final
module; compile with `-fno-common`. Livepatch modules require their original
symbol-table ordering, indices, and additional relocation metadata, which this
release policy does not claim to preserve.

Detection uses redundant signals: any `SHN_LIVEPATCH` symbol, `.klp.*` section,
`SHF_RELA_LIVEPATCH` flag, or NUL-separated `.modinfo` field beginning with
`livepatch=` marks a livepatch module and fails closed. The `.modinfo` marker
alone is sufficient even when no `.klp.*` section or livepatch relocation flag
is present.

Only eligible `.symtab` names are replaced. A loadable `.ko` still requires
`.symtab`, its linked `.strtab`, and relocations, so generic tools may
legitimately describe it as `not stripped`. Independent stores and interfaces
such as BTF, module exports, `.modinfo`, `__versions`, trace metadata,
`__ksymtab_strings`, `.rodata`, and string literals can still disclose
original names or other identifying text. Ordinary kernel symbol names also
change in kallsyms and diagnostics, reducing the usefulness of symbol-based
ftrace, kprobe/BPF attachment, and crash reports. Use an unstripped debug build
for diagnosis and do not rely on a private symbol's original name in a release
module.

### Finalized Android release plugin boundary

Finalization establishes two independent, fail-closed identity boundaries
around plugin output phases:

- Before any replaceable `ObjectGraph` phase, the graph seal binds every
  retained logical section's `section ID`, `final ordinal`, and exact name. It
  also binds each exact-name symbol's `symbol ID` to its name, class, section,
  value, size, binding, type, and complete `st_other`. The release verifier
  independently recomputes the ordinary structural names.
- After the host establishes a trusted write baseline and before
  `neverc.object.post_write`, the image seal binds every retained logical
  section ordinal/name, the total `.symtab` entry count, and each exact-name
  symbol's name and attributes to its raw `.symtab` `slot`.

The resulting capability matrix is deliberately narrow:

| Phase binding | Finalized Android release behavior |
|---------------|------------------------------------|
| `neverc.object.write` `provider` / `interceptor` | `REJECTED` before it can replace the host-established trusted write baseline |
| `plugin-owned ObjectFormat graph writer` | `REJECTED`; finalized Android release requires the host-owned graph writer that establishes the trusted baseline |
| `observer` | `READ_ONLY`; observers remain permitted and cannot mutate the artifact |
| `neverc.object.post_write` `interceptor` | `VALIDATED`; it may change only non-identity payload bytes that still pass the release verifier, input ABI contract, and both identity seals |

Finalized merge ownership is host-sealed too. Any `MergedImage` or independent
bytes from a `third-party ObjectMergeProvider` are discarded; the
`host-owned graph writer` serializes that provider's verified, finalized
graph. Conversely, `built-in finalized input serialization` bypasses
`external object phases` and feeds the host merger the exact
`audited native bytes`; this internal input step does not bypass the output
boundary above.

Finalization is accepted only with `Android module merge semantics`; it also
requires both a `relocatable output request` and
`relocatable driver configuration`, otherwise it fails `before routing`.
For a finalized Android relocatable release, the `frozen input format`,
`TargetKey.ObjectFormatID`, and `frozen output format` must share
`one format identity`. A mismatch is rejected `before provider dispatch`—also
before route planning or sink creation—so capability preflight and actual
graph-writer dispatch cannot observe different formats.

For ordinary graph-representable input, earlier graph interceptors may run only
while preserving the graph seal and all release semantics. If the input needs
native-image passthrough for facts the `ObjectGraph` cannot represent, every
replaceable `route-matching provider` and every interceptor are rejected. A
provider whose target/CPU/features/object-format/execution-level route differs
from the active route neither runs nor blocks the release; only read-only
observers are allowed. A rejection or validation failure
`before sealed commit` aborts staging and publishes no file. An `AFTER_COMMIT`
observer failure is reported after publication and cannot roll the published
file back.

Do not post-process a `.ko` with `llvm-strip --strip-all` or `objcopy`, and do
not blindly remove codetag/BTF/ABI sections. If module signing is used, strip
first and sign the final bytes because any post-signing mutation invalidates the
signature. A `clean` target must only delete files—it must never strip or sign an
existing module.

## Security boundary

Stripping removes high-value naming and debug metadata, which raises the cost
of analysis, but it does not make native machine code impossible to reverse
engineer. A correct stripped binary may still contain:

- dynamic import and export names required by the loader;
- loader-required names and names stored outside `.symtab` in a `.ko`;
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
The negated `strings` check below is expected to find no match and exits
successfully only in that case.

```bash
llvm-readobj --sections --symbols --dyn-symbols app
llvm-dwarfdump app
! strings app | grep -Fq -- neverc_private_release_symbol
test ! -e app.dSYM

file examples/android-kernel-hello/nvk_hello.ko
llvm-readelf -h -S -s -r examples/android-kernel-hello/nvk_hello.ko
llvm-dwarfdump examples/android-kernel-hello/nvk_hello.ko
```

For a loadable ELF `ET_REL` `.ko`, the generic `file` utility may still report
`not stripped` because `.symtab` is deliberately retained. Do not use that
label as the release pass/fail signal. Instead verify that DWARF and `.comment`
are absent, eligible definitions use the canonical `fn_`/`obj_`/`code_`/
`sym_`/`abs_` uppercase-hex forms, `SHN_UNDEF` imports and required
loader/CFI names remain exact, and relocations are valid. Audit BTF, exports,
modinfo, versions, trace metadata, and strings separately if name disclosure
matters.

A stripped artifact should have no source-level debug sections or private
static symbol names. Required dynamic names and runtime metadata are expected
and should not be treated as a stripping failure.
