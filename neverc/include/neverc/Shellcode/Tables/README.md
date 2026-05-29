# neverc/Shellcode/Tables — external data tables for shellcode passes

The `.def` files in this directory factor every large, copy-paste-style
string table out of the shellcode passes and into plain text.  They
follow the classic LLVM / NeverC X-macro pattern (see `Builtins.def`,
`AttrList.def`, `DiagnosticSemaKinds.def`, ...): a table is pure data,
each row is one macro invocation, and consuming sites `#include` the
file after defining the expected macro.

## Why this shape

* **Zero runtime cost.** Everything still lowers to `static constexpr`
  arrays — identical to the previous inline tables.
* **No parser, no filesystem.** Adding a row is one text edit; the
  compiler does the rest.
* **Data lives next to its semantic header.** `SyscallTables.h` declares
  the `SyscallLookup` API; `Tables/Syscalls_*.def` holds the numbers.
  Readers find the two together.
* **User-overridable.** A `UserExtra_<Table>.def` sibling is appended
  after every bundled table.  CMake exposes one cache variable per
  table to point at an external `.def`; leaving it unset silently
  appends an empty file.

## Tables at a glance

| File                                | Consumed by                          | Macro                                     |
|-------------------------------------|--------------------------------------|-------------------------------------------|
| `DefaultEntryNames.def`             | `ExtractorCommon.cpp` (`isDefaultEntryName`) | `NEVERC_DEFAULT_ENTRY(spelling)`     |
| `ShellcodeIRHelperNames.def`        | multiple IR passes (`ZeroRelocPass`, `MemIntrinPass`, etc.) | `NEVERC_SHELLCODE_IR_HELPER(name)` |
| `ShellcodeInternalRuntimePrefixes.def` | `ExtractorCommon.cpp` (`isShellcodeInternalRuntimeName`) | `NEVERC_SHELLCODE_INTERNAL_RUNTIME_PREFIX(prefix)` |
| `KernelImportReservedPrefixes.def`  | `KernelImportPass.cpp`               | `NEVERC_KERNEL_IMPORT_RESERVED_PREFIX(prefix)` |
| `Syscalls_Darwin.def`               | `SyscallTables.cpp`                  | `NEVERC_SYSCALL(name, number)`            |
| `Syscalls_LinuxArm64.def`           | `SyscallTables.cpp`                  | `NEVERC_SYSCALL(name, number)`            |
| `Syscalls_LinuxX86_64.def`          | `SyscallTables.cpp`                  | `NEVERC_SYSCALL(name, number)`            |
| `SyscallLikelyNames.def`            | `SyscallTables.cpp` (diagnostic)     | `NEVERC_SYSCALL_LIKELY(name)`             |
| `Win32Apis.def`                     | `WinImportTables.cpp`                | `NEVERC_WIN32_API(name, dll)`             |
| `PosixCanonicalTypes.def`           | `SyscallStub.cpp`                    | `NEVERC_POSIX_CANON(name, ret, args)` (macro-rich; see header of file) |
| `PosixToWin32Alt.def`               | `ExtractorCommon.cpp`                | `NEVERC_POSIX_WIN32(bareName, win32Name)` |
| `ReservedMemStdlibNames.def`        | `ExtractorCommon.cpp` (`isReservedMemStdlibName`) | `NEVERC_NAME(name)`      |
| `ComplexLibcallNames.def`           | `ExtractorCommon.cpp`                | `NEVERC_NAME(name)`                       |
| `LibmTranscendentalNames.def`       | `ExtractorCommon.cpp` (`isLibmTranscendentalName`) | `NEVERC_NAME(name)`                   |
| `StdioCallNames.def`                | `ExtractorCommon.cpp` (`isStdioCallName`) | `NEVERC_NAME(name)`                   |
| `ScalarSoftFloatNames.def`          | `ExtractorCommon.cpp` (`isScalarSoftFloatHelperName`) | `NEVERC_NAME(name)`                       |
| `LongIntegerHelperPrefixes.def`     | `ExtractorCommon.cpp` (`isLongIntegerCompilerRtHelperName`) | `NEVERC_PREFIX(prefix)`       |
| `CompilerGeneratedExternHints.def`  | `ExtractorCommon.cpp` (`getExternalSymbolHint`) | `NEVERC_EXTERN_HINT(name, message)`       |
| `StackProbeNames.def`              | `CompilerRtPass.cpp` (erase declarations, stamp attribute) | `NEVERC_NAME(name)`      |
| `Binary128HelperNames.def`          | `ExtractorCommon.cpp` (`isBinary128HelperName`) | `NEVERC_NAME(name)`               |
| `HeapAllocatorNames.def`            | `ExtractorCommon.cpp` (`isHeapAllocatorName`) | `NEVERC_NAME(name)`                   |
| `HeapArenaRewriteTargets.def`      | `HeapArenaPass.cpp` (`classifyHeapCall`) | `NEVERC_HEAP_ARENA_TARGET(name, kind)` |
| `StringRuntimeAllocatorNames.def`   | `StringRuntimePass.cpp`              | `NEVERC_STRING_RUNTIME_ALLOCATOR(name, role)` |
| `LibcInlineHelpers.def`             | `MemIntrinPass.cpp`                  | `NEVERC_LIBC_INLINE_HELPER(externName, helperSlot, factory)` |
| `CompilerRtExternBinds.def`         | `CompilerRtPass.cpp`                 | `NEVERC_COMPILER_RT_EXTERN_BIND(externName, helperSlot, factory)` |
| `SetjmpNames.def`                   | `ExtractorCommon.cpp` (`isSetjmpName`) | `NEVERC_NAME(name)`                   |
| `KernelHelperNames.def`             | `ExtractorCommon.cpp` (`lookupKernelHelperOS`) | `NEVERC_KERNEL_HELPER(name, os)` |
| `MIRRewritePatterns.def`            | `MIRPrepPass.cpp`                    | `NEVERC_MIR_REWRITE_PATTERN(id, display, arch, function)` |
| `MIRRewriteOpcodes.def`             | `MIRPrepPass.cpp`                    | `NEVERC_MIR_REWRITE_OPCODE(pattern, role, opcode)` |
| `Win32PosixCompat.def`              | `WinPEBImport.cpp`                   | `NEVERC_WIN32_POSIX_COMPAT(bareName, wrapperBuilder)` |
| `MIRStripPseudoOpcodes.def`         | `MIRPrepPass.cpp`                    | `NEVERC_MIR_STRIP_PSEUDO(opcode, category)` |

Each `.def` starts with a short comment block, an `#ifndef ... #error`
guard that names the expected macro, and one row per entry.  No row
uses control-flow syntax directly; `.def` files must stay plain rows
so the same source can be consumed with different expansions (e.g. to
generate a textual manifest in the future).  Some rows carry string
literals or helper identifiers when that is the data being externalised
(`Win32Apis`, `MIRRewritePatterns`).

## Adding a new entry (upstream)

Open the right `.def` and add a line:

```
NEVERC_SYSCALL(getrandom, 278)         // LinuxArm64
NEVERC_WIN32_API(GetOverlappedResult, "kernel32.dll")
```

Rebuild — the new row is picked up automatically.

## Adding entries as a user (out-of-tree)

Write your own `.def` file using the same `NEVERC_*` macro as the
bundled table (no need to redefine the macro; it is already in scope
when the `.cpp` includes your file).

```
// /work/extras/my_syscalls.def
NEVERC_SYSCALL(foo_syscall, 1234)
NEVERC_SYSCALL(bar_syscall, 1235)
```

Then point CMake at it:

```
cmake -DNEVERC_EXTRA_Syscalls_LinuxX86_64=/work/extras/my_syscalls.def ...
```

The variable name matches the bundled table stem: `NEVERC_EXTRA_<Stem>`
for every file in the table above.  CMake copies the file into the
build tree as `Tables/UserExtra_<Stem>.def`; if the variable is empty
an empty file is generated so every include always succeeds.

Tables you are likely to extend:

* `Syscalls_*` — add a new kernel entry point.
* `Win32Apis` — resolve a new API through the PEB walker.
* `PosixCanonicalTypes` — teach the K&R fallback a new signature.
* `PosixToWin32Alt` — suggest a new POSIX-to-Win32 replacement in the
  diagnostic for Windows targets.
* `ReservedMemStdlibNames` — tell the kernel resolver and the mem/str
  diagnostic to ignore a custom libc-shaped symbol.
* `KernelHelperNames` — teach the extractor and shared MIR extern diagnostic
  about a new ring-0 helper symbol.  The second argument is the OS
  tag (`linux` / `windows` / `darwin`) that owns the helper.  Android
  kernel helpers reuse the `linux` tag because the Android kernel is
  a GKI/KMI Linux kernel and bionic exists only in user space.
* `CompilerGeneratedExternHints` — add an exact MIR diagnostic for a
  compiler-generated helper that shellcode mode should normally prevent
  with driver flags or earlier IR passes.
* `StackProbeNames` — list additional stack-probe extern spellings
  (`__chkstk` variants) that `CompilerRtPass` forcibly erases and whose
  emission is prevented by stamping `no-stack-arg-probe` on all
  functions.
* `LongIntegerHelperPrefixes` / `Binary128HelperNames` — sharpen
  compiler-rt diagnostics without touching MIR pass control flow.
* `StringRuntimeAllocatorNames` — keep shellcode support for the builtin
  string runtime when downstream code overrides `NEVERC_STRING_ALLOC` /
  `NEVERC_STRING_FREE`.
* `LibcInlineHelpers` — alias an extra libc / stdlib spelling onto an
  existing `MemIntrinPass` inline helper (for instance, downstream code
  that calls `wmemcpy` / `__memcpy_isoc99` / a renamed Mach-O variant).
  Reusing an existing `helperSlot` requires NO C++ changes: reuse the
  same `helperSlot` **and** its matching `factory` in the new row.
  Introducing a brand-new helper still needs a `LibcHelperBundle` field
  plus a `getOrCreate*` factory in `MemIntrinPass.cpp` that the new
  row references.
* `CompilerRtExternBinds` — alias an extra compiler-rt spelling onto
  an existing `CompilerRtPass` inline helper (for example a toolchain
  that emits `___udivti3` with three leading underscores).  Same
  contract as `LibcInlineHelpers`: the new row reuses an existing
  `helperSlot` and its matching factory; a brand-new helper needs a
  `CRTHelperBundle` field + factory in `CompilerRtPass.cpp`.
* `MIRRewritePatterns` — register a built-in MIR rewrite helper, its
  diagnostic name, and its architecture filter.  The helper function
  must live in `MIRPrepPass.cpp`; this table keeps scheduling metadata
  out of the pass body.
* `MIRRewriteOpcodes` — override backend opcode names used by
  `MIRPrepPass` rewrite patterns.  The `opcode` value must match
  `TargetInstrInfo::getName()` output from the backend TableGen files.
* `Win32PosixCompat` — add POSIX aliases that reuse an existing Windows
  compat wrapper builder.  New semantics still require C++ wrapper code,
  but the supported spelling list stays in a table instead of the pass
  dispatch logic.

* `MIRStripPseudoOpcodes` — add a backend pseudo opcode to strip from
  the MIR before shellcode emission.  The `opcode` must match a
  `TargetOpcode::*` enumerator.
* `DefaultEntryNames` — add alternative entry function names that
  `ZeroRelocPass` and the extractors accept (e.g. `payload_main`).
* `ShellcodeInternalRuntimePrefixes` — add a symbol prefix to exclude
  from the external-symbol audit (e.g. `__my_runtime_`).  Every prefix
  must start with `_` or `l`.
* `KernelImportReservedPrefixes` — add a prefix reserved by the kernel
  import resolution shim.

* `ShellcodeIncompats` — driver-level option incompatibilities checked
  before shellcode compilation.  Each row maps an `OPT_*` enum to a
  human-readable reason string.  Add a row when a new driver flag is
  known to produce artifacts that shellcode cannot consume.
* `ShellcodeInjectFlags` — compiler flags the shellcode driver injects
  into every compilation invocation.  Each row is a single flag string
  (e.g. `"-ffreestanding"`).  Add a row to force a flag for all
  shellcode compilations.

## Tables deliberately not externalised

A few small, prefix-based or deeply intertwined with diagnostic text
lists stay inline (`"llvm."` / `"__sc_"` / `"__neverc_"` guards, the
inline-asm template detector in `ExtractorCommon::printExternHint`,
and the structured templates in `SyscallCompat_*`).  Factoring them
out would hide control flow without reducing the maintenance surface.
