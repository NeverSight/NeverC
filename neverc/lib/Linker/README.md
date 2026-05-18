# NeverC Linker

The embedded linker shipped inside the unified `neverc` executable.
There is no standalone linker binary and no progname-based dispatch:
the `neverc` driver picks a `linker::Flavor` (Gnu / Darwin / WinLink)
and calls the matching backend's `link()` through a static
`linker::DriverDef[]` table built in `neverc/main.cpp`.  Every
backend is linked statically into the same tool.

Three structural choices keep the tree easy to navigate:

1. The shared foundation lives under `Core/` split into three
   purpose-built sub-folders — **Driver**, **Runtime**, **Support** —
   see **Core layout** below.
2. CMake plumbing collapses to *two* helper calls declared in
   `cmake/modules/LinkerLibrary.cmake` (`linker_add_library` +
   `linker_declare_backend`), so every backend `CMakeLists.txt`
   describes *what it is* instead of *how to build it*, and the Core
   tree is a single plain `linker_add_library` call.
3. Backend implementation files sit under responsibility buckets such
   as `Driver/`, `Input/`, `Layout/`, `Symbols/`, `Debug/`,
   `Transforms/`, `LTO/`, and `Emit/`, while public include paths
   stay stable under `include/Linker/<Format>/`.

---

## Directory layout

```text
neverc/lib/Linker/
├── CMakeLists.txt                          top-level entrypoint + backend
│                                           registry (~120 lines)
│
├── cmake/modules/LinkerLibrary.cmake       the only CMake helper module:
│                                             · linker_add_library
│                                             · linker_declare_backend
│
├── Core/                                   shared foundation → linkerCore
│   ├── CMakeLists.txt                        single linker_add_library call
│   │                                         assembling all three buckets
│   │
│   ├── Driver/                               argv plumbing + codegen flags
│   │   ├── ArgList.cpp                        llvm::opt::InputArgList ext
│   │   └── CodegenFlags.cpp                   LLVM codegen flags bridge
│   │
│   ├── Runtime/                              per-link lifetime: session,
│   │   │                                     allocator, diagnostics,
│   │   │                                     stopwatch
│   │   ├── Session.cpp                        CommonLinkerContext owner
│   │   ├── Allocator.cpp                      SpecificAlloc<T> pool
│   │   ├── Diagnostic.cpp                     error/warn/log + VS mode
│   │   └── Stopwatch.cpp                      scoped and nested timers
│   │
│   └── Support/                              generic helpers glued to the
│       │                                     LLVM surface
│       ├── Strings.cpp                        StringMatcher + text utils
│       ├── FileIO.cpp                         tryCreateFile / openFile +
│       │                                      async unlink
│       └── Dwarf.cpp                          DWARF line-info + variable
│                                              location cache
│
├── Backends/
│   ├── COFF/                               Windows PE/COFF → linkerCOFF
│   │   ├── Driver/ Input/ Layout/ Symbols/ Emit/
│   │   └── Debug/ Transforms/ LTO/ State/
│   ├── ELF/                                ELF + Targets/    → linkerELF
│   │   ├── Targets/ Driver/ Input/ Layout/ Symbols/ Emit/
│   │   └── Debug/ Transforms/ LTO/
│   └── MachO/                              Mach-O + Targets/ → linkerMachO
│       ├── Targets/ Driver/ Input/ Layout/ Symbols/ Emit/
│       └── Debug/ Transforms/ LTO/
│
└── include/Linker/                         public headers (installed as-is)
    ├── Core/
    │   ├── Driver/{Dispatcher,ArgList,CodegenFlags}.h
    │   ├── Runtime/{Session,Allocator,Diagnostic,Stopwatch}.h
    │   └── Support/{Strings,FileIO,Dwarf,
    │                Chunks,LlvmAliases}.h
    ├── COFF/
    ├── ELF/
    └── MachO/
        └── Targets/                           ARM64Common.h (Mach-O)
```

Every translation unit reaches headers through the stable
`linker/<area>/<bucket>/<name>.h` path — there are no relative `"Foo.h"`
includes anywhere in the tree.  For `linkerCore`, `include/Linker/Core/`
mirrors `Core/` one-to-one.  Backend sources live under
`Backends/<Format>/<Bucket>/` while their public headers stay at
`include/Linker/<Format>/` (stable include paths independent of where
the `.cpp` files sit on disk).

---

## Core layout

| Bucket     | Files                                                                     | Responsibility                                                                                      |
|------------|---------------------------------------------------------------------------|-----------------------------------------------------------------------------------------------------|
| `Driver/`  | `ArgList`, `CodegenFlags`                                                  | `llvm::opt::InputArgList` accessors, `llvm::codegen` bridge. |
| `Runtime/` | `Session`, `Allocator`, `Diagnostic`, `Stopwatch`                         | Per-link lifetime (`CommonLinkerContext`, on-demand `SpecificAlloc<T>` arena), `ErrorHandler` with `error`/`warn`/`log`/`fatal` front door, scoped hierarchical phase timers. |
| `Support/` | `Strings`, `FileIO`, `Dwarf`, `Chunks` (hdr), `LlvmAliases` (hdr)         | String / hex / glob helpers, output-path creation + async unlink, DWARF line/variable-location cache, byte-array splitter, single-include LLVM ADT alias set. |

Each bucket is a plain sub-folder — there is no CMake-level "layer"
abstraction.  `Core/CMakeLists.txt` lists the sources under per-bucket
variables (`linker_core_driver`, `linker_core_runtime`,
`linker_core_support`), hands them all to a single
`linker_add_library(linkerCore …)` call, and tags each bucket with a
matching `source_group` so IDEs present the library as three neatly
separated folders.

---

## Backend layout

Each backend keeps `CMakeLists.txt` and `Options.td` at the format root
(`Backends/COFF`, `Backends/ELF`, `Backends/MachO`) because tablegen
looks for `Options.td` relative to the backend CMake directory.  The
implementation files below that root are grouped by responsibility:

COFF support is a first-class target in NeverC. Do not remove
`Backends/COFF` / `include/Linker/COFF` or gate it off by default in
shared build presets unless there is an explicit, approved deprecation
plan.

| Bucket          | Responsibility                                                                      |
|-----------------|-------------------------------------------------------------------------------------|
| `Driver/`       | option parsing and format entry points                                              |
| `Input/`        | object/archive/shared-library ingestion and input section modeling                  |
| `Layout/`       | output sections, segments, scripts, thunks, relocations (no final on-disk image TU)   |
| `Symbols/`      | symbol records, symbol tables and exported-symbol data structures                   |
| `Debug/`        | DWARF, unwind and frame metadata                                                    |
| `Transforms/`  | mark-live, ICF, call-graph sorting and format-specific layout fixes                 |
| `LTO/`          | LLVM LTO integration                                                                |
| `State/`        | backend-specific link state (`COFF` only today: `COFFLinkerContext`)                |
| `Emit/`         | PE/ELF/Mach-O image emission + link-map (`.map`) — same bucket on every backend     |
| `Targets/`     | target-specific relocations and thunks (ELF and Mach-O; optional extra headers)        |

This is a physical source-tree split only.  Header includes remain
`linker/<Format>/...`, namespaces are unchanged, and each backend still
builds as one static library.

---

## Link map (`.map`) emission

Every backend places its map emitter under `Emit/`
(`Emit/{Coff,Elf,MachO}LinkMapEmitter.cpp`), but the three
implementations are **intentionally independent** — there is no
shared map IR and no shared printer.  Each backend mimics the map
format of the native toolchain for its object format:

| Backend | Format style                                                                                                                 |
|---------|------------------------------------------------------------------------------------------------------------------------------|
| ELF     | GNU `ld` style — `VMA / LMA / Size / Align` columns with Out → In → Symbol indentation; optional `--cref` cross-reference table |
| COFF    | MSVC `link.exe` style — `sec:rva` addressing, `Rva+Base` column, `Timestamp`, `Static symbols`, `Exports`, entry point line   |
| Mach-O  | Apple `ld` style — `# Path / # Arch / # Object files / # Sections / # Symbols` headers, `[idx] file` ordinals, `<<dead>>` rows |

This split is deliberate, for three reasons:

1. **Per-format conventions.** Each emitter mirrors the native
   toolchain map exactly so downstream tools (PDB tooling, Xcode
   `symbolicate`, GNU `ld` consumers) keep working without
   reformatting.
2. **Toolchain compatibility.** The native formats are consumed by
   external tools: PDB / debugger tooling on Windows expects the
   `link.exe` shape, and `symbolicate` / Xcode crash symbolication on
   macOS expects the Apple `ld` shape.  Replacing either with the ELF
   format would silently break those consumers.
3. **Data model mismatch.** The ELF emitter depends on ELF-only
   concepts that have no faithful equivalent in COFF/Mach-O —
   distinct `VMA` vs `LMA`, `LinkerScript` commands
   (`SymbolAssignment`, `ByteCommand`, `InputSectionDescription`),
   `EhFrameSection` piece aggregation, and a strict
   Out → InputSection → Symbol hierarchy.  COFF lives in
   `RVA + ImageBase` with `Chunk` ranges; Mach-O adds a Segment level
   above Section and synthesises pseudo-entries for cstrings, stubs,
   GOT and `compact unwind info`.

If a future feature ever needs a *cross-platform* link map (e.g. for a
unified post-link analysis tool), the intended landing spot is a new
`LinkMapModel` IR in `Core/` that all three backends populate, paired
with an extra ELF-style printer selectable via a flag — **not** a
rewrite of an existing backend's printer.  Until that need materialises
the three emitters stay independent.

---

## Library graph

```text
             ┌──────────────────────┐
             │     neverc (tool)    │
             └──────────┬───────────┘
                        │
         ┌──────────────┼──────────────┐
         ▼              ▼              ▼
   linkerCOFF      linkerELF      linkerMachO
         │              │              │
         └──────────────┼──────────────┘
                        ▼
                   linkerCore
                        │
                        ▼
                      LLVM*
```

- `linkerCore` — the fork-specific foundation.  Owns the diagnostics
  and the hierarchical phase timer (`Core/Runtime/Diagnostic.cpp`,
  `Core/Runtime/Stopwatch.cpp`), the arena-backed
  `CommonLinkerContext` (`Core/Runtime/Session.cpp`,
  `Core/Runtime/Allocator.cpp`) and the DWARF line-info cache
  (`Core/Support/Dwarf.cpp`).
- Each backend library (`linkerCOFF`, `linkerELF`, `linkerMachO`) is
  self-contained; there are no cross-backend `#include`s.  They all
  depend on `linkerCore`.
- The single entry point lives in `neverc/main.cpp`,
  which builds a `linker::DriverDef[]` via
  `LINKER_HAS_DRIVER(coff|elf|macho)` (declared in
  `linker/Core/Driver/Dispatcher.h`) and, inside the
  `Driver::LinkerMain` lambda, calls the matching backend's `link()`
  directly based on the `linker::Flavor` provided by the driver
  pipeline.

---

## CMake helpers

`cmake/modules/LinkerLibrary.cmake` exposes exactly two functions.
Every `CMakeLists.txt` in this tree uses one of them; there are no
hand-rolled `llvm_add_library` calls outside the module.

### `linker_add_library(<target> [SHARED] <llvm_add_library args>)`

Thin wrapper around `llvm_add_library` used internally to declare
linker libraries.  It tags the target for IDE browsing, installs it
under the standard LLVM tree, and records it under the global
`LINKER_EXPORTS` property so it can be re-exported from the top-level
install tree.

### `linker_declare_backend(NAME <tag> SOURCES ... [EXTRA_COMPONENTS ...] [EXTRA_LIBS ...] [EXTRA_DEPS ...])`

Declares one object-format backend in a single call:

- runs the standard `Options.td → Options.inc` tablegen step and
  registers a public target named `<tag>OptionsTableGen`,
- produces a static library called `linker<tag>`,
- links `linkerCore` + `LINKER_BACKEND_LLVM_COMPONENTS` (AArch64 /
  X86 / MC / Option / LTO / …) automatically,
- attaches `ADDITIONAL_HEADER_DIRS` pointing at
  `include/Linker/<tag>` so IDEs pick up the headers.

Callers supply only the delta (extra LLVM components, platform libs,
extra dependencies).  A typical backend `CMakeLists.txt` is 15–50 lines
end to end; see `Backends/COFF/CMakeLists.txt` for the reference shape.

`linkerCore` itself is built with a plain `linker_add_library` call
in `Core/CMakeLists.txt`; there is no per-bucket helper because the
three Core sub-folders are organisational buckets, not independent
libraries.

---

## Options & tablegen

Every backend owns an `Options.td` that is compiled to `Options.inc` by
`linker_declare_backend`.  The generated file sits next to the backend
under the build directory and is referenced as `#include "Options.inc"`
from `CoffDriver.cpp` / `ElfDriver.cpp` / `MachODriver.cpp` and each
backend's `*CommandLine.cpp` (`CoffCommandLine.cpp`, `ElfCommandLine.cpp`,
`MachOCommandLine.cpp`).  There is no shared options
table.

### `LinkerDriverConfig`

Since `neverc` is both compiler and linker in a single binary, many
options that a standalone linker would parse from its own command line
are instead conveyed by the NeverC driver through a
`LinkerDriverConfig` struct
(declared in `linker/Core/Driver/Dispatcher.h`).  This avoids
double-parsing and keeps the user-facing flag surface on the `neverc`
driver, not on the embedded linker.

Options removed from per-backend `Options.td` and routed through
`LinkerDriverConfig`:
- All LTO pipeline tuning: optimization level, codegen opt level,
  partitions, basic-block-sections, unique-basic-block-section-names.
- LTO diagnostics: opt-remarks.
- Shared driver-level settings: save-temps, time-trace, error-limit,
  verbose, trace (-t/--trace), threads, sysroot, demangle, nostdlib,
  fatal-warnings, suppress-warnings, compress-debug-sections, mllvm,
  pass-plugins, linker-opt-level (-O), reproducible (MachO/COFF).
- Linker optimization defaults: gc-sections, dead_strip (MachO),
  eh-frame-hdr, icf level, build-id, strip level, hash-style.

Options that remain in `Options.td` are genuine linker-specific flags
(e.g. `--debug`, linker scripts, `-z` extensions) that the NeverC
driver constructs on the linker command line.  All three platforms now
use unified ELF-style option syntax (`--option=value`).  Options like
`--gc-sections`, `--build-id`, `--icf=`, `--eh-frame-hdr`, `--hash-style`,
`--strip-all`, `--strip-debug`, `--pie`, `--shared`, `--relocatable`,
`--dynamic-linker`, `--dll`, `--WX`, `-pie`/`-no_pie`, `-execute`/`-dylib`/
`-bundle`, `-static`/`-dynamic`, `--no-deduplicate` have been removed from
`Options.td`; they are now exclusively controlled by `LinkerDriverConfig`
fields set by the neverc driver. Passing the old spellings through `-Wl` or
`/link` is unsupported: the embedded linker table no longer defines those IDs,
so they surface as unknown arguments.

---

## Naming conventions

| Thing                     | Convention                                              |
|---------------------------|---------------------------------------------------------|
| Include guards (Core)     | `LINKER_CORE_<BUCKET>_<HEADER>_H`                       |
| Include guards (backends) | `LINKER_<BACKEND>_<HEADER>_H`                           |
| Library targets           | `linkerCore`, `linkerCOFF`, `linkerELF`, `linkerMachO`  |
| Tablegen targets          | `<Backend>OptionsTableGen`                              |
| C++ namespace             | `linker::` (sub-namespaces `linker::coff`, etc.)        |
| Backend entry `.cpp`      | Format-prefixed drivers; CLI helpers in `*CommandLine.cpp`; image/map in `Emit/*`     |

Only `LINKER_*` / `linker::` identifiers are used in this tree; the
runtime class names (`CommonLinkerContext`, `ErrorHandler`,
`ScopedTimer`, `Timer`, …) deliberately reflect what they do rather
than where they came from.

---

## Building

The linker only ever builds as part of the in-tree NeverC build:

```bash
cmake -S llvm -B build-neverc -G Ninja \
      -C neverc/cmake/caches/NeverC.cmake
./build-neverc.sh
```

Backends can be toggled individually:

```bash
cmake -DLINKER_ENABLE_COFF=OFF …   # skip Windows PE/COFF
cmake -DLINKER_ENABLE_ELF=OFF …    # skip ELF
cmake -DLINKER_ENABLE_MACHO=OFF …  # skip Mach-O
```

---

## Adding a new backend

1. Drop `Options.td` at `neverc/lib/Linker/Backends/<NewFormat>/` and place
   implementation files in the backend responsibility buckets (`Driver/`,
   `Input/`, `Layout/`, etc.).
2. Create `neverc/lib/Linker/Backends/<NewFormat>/CMakeLists.txt` with a single
   `linker_declare_backend(NAME <NewFormat> SOURCES … EXTRA_COMPONENTS …)`
   call.
3. Create `neverc/include/neverc/Linker/<NewFormat>/` for the public
   headers.
4. Append `<NewFormat>` to `LINKER_BACKEND_DIRS` in the top-level
   `CMakeLists.txt` (the tree path is always `Backends/<NewFormat>/`) and
   declare a matching `LINKER_ENABLE_<UPPER>` option.
5. Call `LINKER_HAS_DRIVER(<newformat>)` in
   `neverc/main.cpp`, add a new `linker::Flavor`
   value in `linker/Core/Driver/Dispatcher.h`, and append a
   `{linker::Flavor::<Value>, &linker::<newformat>::link}` entry to the
   `EnabledLinkerDrivers[]` table.

No changes to `LinkerLibrary.cmake` are required.

---

## Adding a new Core file

1. Drop sources + headers under `Core/<Bucket>/` and
   `include/Linker/Core/<Bucket>/` (pick the existing bucket whose
   responsibility matches — `Driver`, `Runtime` or `Support`).
2. Append the source file to the matching `linker_core_<bucket>`
   variable in `Core/CMakeLists.txt` — the list is the only thing
   `Core/CMakeLists.txt` needs to know about it.

If a genuinely new responsibility appears (i.e. no existing bucket
fits), add a new sub-folder and a matching `linker_core_<bucket>`
list in `Core/CMakeLists.txt`, then feed it into the single
`linker_add_library(linkerCore …)` call.

---

## Architectural choices

The backends share the ELF/MachO/COFF object-format data models but
the pipeline, class hierarchy, and public API are NeverC-specific.
Key structural choices:

- **Three-bucket Core layout.** Shared code lives in `Core/Driver`,
  `Core/Runtime` and `Core/Support` so the directory tree maps onto
  the logical role of each file.
- **Public header layout.** `include/Linker/Core/...` mirrors
  `Core/...`.  Backend headers live at `include/Linker/<Format>/...`
  while implementation files sit under `Backends/<Format>/<Bucket>/...`;
  there is no cross-bucket `"Foo.h"` include.
- **Backend responsibility buckets.**  Implementation files are
  grouped under `Driver/`, `Input/`, `Layout/`, `Emit/`, `Symbols/`,
  `Debug/`, `Transforms/`, `LTO/`, and per-backend buckets like
  `State/` (COFF).  `Emit/` holds the on-disk image emission and
  link-map (`.map`) translation units on **every** backend.
- **Core file inventory.**

  | File                                | Responsibility                              |
  |-------------------------------------|---------------------------------------------|
  | `Driver/ArgList.cpp`                | `llvm::opt::InputArgList` extensions        |
  | `Driver/CodegenFlags.cpp`           | LLVM codegen flags bridge                   |
  | `Driver/CommonLTOConfig.cpp`        | shared LTO configuration                    |
  | `Runtime/Session.cpp`               | `CommonLinkerContext` owner                 |
  | `Runtime/Allocator.cpp`             | `SpecificAlloc<T>` pool                     |
  | `Runtime/Diagnostic.cpp`            | error/warn/log + VS mode                    |
  | `Runtime/Stopwatch.cpp`             | scoped and nested timers                    |
  | `Support/Strings.cpp`               | matcher + text utilities                    |
  | `Support/FileIO.cpp`                | output-path creation + async unlink         |
  | `Support/Dwarf.cpp`                 | DWARF line / variable-location cache        |

- **Pipeline class names.** The on-disk writer is `OutputWriter` with
  `writeOutput()`; the per-flavor entry point is `LinkerDriver::execute()`;
  layout helpers are `completeLayout()`, `computeFileLayout()`,
  `buildSegmentMap()`, `commitSegmentHeaders()`, `validateLayout()`,
  `prepareOutputFile()`, `materializeContent()`, `sealBuildId()`.
  `demoteAndCopyLocalSymbols()` uses two-phase parallel processing.
- **`LINKER_*` / `linker::` namespace** throughout.
- **No standalone linker executable.** `neverc` hosts every flavor by
  dispatching `linker::Flavor` to the matching backend `link()`.
- **Data-parallel pipeline.**  The ELF backend parallelises section
  aggregation per-file, local symbol demotion, preemptibility
  computation, binding computation, and reuses the existing output
  file's filesystem blocks via in-place overwrite.  The MachO LTO path
  computes per-file resolutions in parallel before serially handing
  them to the LTO engine, and the UUID is a two-level parallel xxhash.
- **Shared CMake API.**  `linker_add_library` and
  `linker_declare_backend` hide the tablegen plumbing and the
  per-backend LLVM component list.
