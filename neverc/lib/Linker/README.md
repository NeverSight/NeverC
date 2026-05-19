# NeverC Linker

Embedded linker inside the unified `neverc` executable.
No standalone binary — `neverc` selects a `linker::Flavor` and calls the
matching backend's `link()` through a static `linker::DriverDef[]` table.

---

## Directory layout

```text
neverc/lib/Linker/
├── CMakeLists.txt                        top-level: options + backend registry
├── cmake/modules/LinkerLibrary.cmake     linker_add_library + linker_declare_backend
│
├── Core/                                 shared foundation → linkerCore
│   ├── Driver/                             ArgList, CodegenFlags, CommonLTOConfig
│   ├── Runtime/                            Session, Allocator, Diagnostic, Stopwatch
│   └── Support/                            Strings, FileIO, Dwarf
│
├── Backends/
│   ├── COFF/                             → linkerCOFF
│   │   └── Driver/ Input/ Layout/ Symbols/ Emit/ Transforms/ LTO/ State/
│   ├── ELF/                              → linkerELF
│   │   └── Targets/ Driver/ Input/ Layout/ Symbols/ Emit/ Debug/ Transforms/ LTO/
│   └── MachO/                            → linkerMachO
│       └── Targets/ Driver/ Input/ Layout/ Symbols/ Emit/ Debug/ Transforms/ LTO/
│
└── (headers at neverc/include/neverc/Linker/{Core,COFF,ELF,MachO}/)
```

Headers use stable `Linker/<area>/<name>.h` include paths; no relative
`"Foo.h"` includes.

---

## Core

| Bucket     | Files                                    | Purpose                                          |
|------------|------------------------------------------|--------------------------------------------------|
| `Driver/`  | ArgList, CodegenFlags, CommonLTOConfig   | `InputArgList` extensions, codegen flags, shared LTO config |
| `Runtime/` | Session, Allocator, Diagnostic, Stopwatch | `CommonLinkerContext`, arena allocator, error handling, timers |
| `Support/` | Strings, FileIO, Dwarf + headers: Chunks, LlvmAliases | Text utils, file I/O, DWARF cache |

All three buckets compile into one `linkerCore` library via a single
`linker_add_library` call.

---

## Backend buckets

| Bucket        | Purpose                                                   |
|---------------|-----------------------------------------------------------|
| `Driver/`     | Option parsing and format entry point                     |
| `Input/`      | Object/archive/shared-lib ingestion                       |
| `Layout/`     | Output sections, segments, scripts, thunks, relocations   |
| `Symbols/`    | Symbol records and symbol tables                          |
| `Debug/`      | DWARF, unwind, frame metadata (ELF + MachO only)         |
| `Transforms/` | Mark-live, ICF, call-graph sorting                        |
| `LTO/`        | LLVM LTO integration                                     |
| `State/`      | Backend-specific link state (COFF only: `COFFLinkerContext`) |
| `Emit/`       | Image emission + link-map (`.map`)                        |
| `Targets/`    | Target-specific relocations and thunks (ELF + MachO)     |

Each backend builds as one static library; no cross-backend `#include`s.

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

---

## CMake helpers

`LinkerLibrary.cmake` provides two functions:

- **`linker_add_library`** — wraps `llvm_add_library` with install/export
  rules. Used by `linkerCore`.
- **`linker_declare_backend`** — declares a backend library `linker<tag>`,
  auto-links `linkerCore` + shared LLVM components. Each backend
  `CMakeLists.txt` is 15–50 lines.

---

## Options

Each backend owns an `Options.td.h` that is directly `#include`d from
its `*CommandLine.cpp` and `Driver.h`.

Many options that a standalone linker would parse from its own argv are
instead forwarded by the `neverc` driver through a `LinkerDriverConfig`
struct (declared in `Linker/Core/Driver/Dispatcher.h`). This includes
LTO tuning, save-temps, threads, sysroot, gc-sections, ICF, build-id,
strip level, etc. Options remaining in `Options.td.h` are genuine
linker-specific flags (e.g. `--debug`, linker scripts, `-z` extensions).

COFF support is a first-class target — do not remove or gate it off
without an explicit deprecation plan.

---

## Building

```bash
cmake -S llvm -B build-neverc -G Ninja \
      -C neverc/cmake/caches/NeverC.cmake
./build-neverc.sh
```

Toggle backends individually:

```bash
cmake -DLINKER_ENABLE_COFF=OFF …
cmake -DLINKER_ENABLE_ELF=OFF …
cmake -DLINKER_ENABLE_MACHO=OFF …
```

---

## Adding a new backend

1. Create `Backends/<NewFormat>/` with `Options.td.h` and source files
   in responsibility buckets (`Driver/`, `Input/`, `Layout/`, etc.).
2. Add a `CMakeLists.txt` with a single `linker_declare_backend(NAME ...)` call.
3. Create `neverc/include/neverc/Linker/<NewFormat>/` for public headers.
4. Append to `LINKER_BACKEND_DIRS` in the top-level `CMakeLists.txt`
   and declare a `LINKER_ENABLE_<UPPER>` option.
5. Register the flavor in `Dispatcher.h` and wire it up in `neverc/main.cpp`.

## Adding a new Core file

1. Drop source + header under `Core/<Bucket>/` and
   `include/Linker/Core/<Bucket>/` (pick from Driver, Runtime, Support).
2. Append to the matching `linker_core_<bucket>` list in `Core/CMakeLists.txt`.

---

## Naming conventions

| Thing                     | Convention                                             |
|---------------------------|--------------------------------------------------------|
| Include guards (Core)     | `LINKER_CORE_<BUCKET>_<HEADER>_H`                     |
| Include guards (backends) | `LINKER_<BACKEND>_<HEADER>_H`                         |
| Library targets           | `linkerCore`, `linkerCOFF`, `linkerELF`, `linkerMachO` |
| C++ namespace             | `linker::` (sub-namespaces `linker::coff`, etc.)       |
