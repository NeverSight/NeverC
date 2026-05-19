# NeverC Linker

Embedded linker inside `neverc`. No standalone binary — the driver
selects a `linker::Flavor` and calls the matching backend's `link()`.

---

## Layout

```text
neverc/lib/Linker/
├── cmake/modules/LinkerLibrary.cmake    linker_add_library + linker_declare_backend
│
├── Core/                                → linkerCore
│   ├── Driver/                            ArgList, CodegenFlags, CommonLTOConfig
│   ├── Runtime/                           Session, Allocator, Diagnostic, Stopwatch
│   └── Support/                           Strings, FileIO, Dwarf
│
├── Backends/
│   ├── COFF/                            → linkerCOFF
│   │   └── Driver/ Input/ Layout/ Symbols/ Emit/ Transforms/ LTO/ State/
│   ├── ELF/                             → linkerELF
│   │   └── Targets/ Driver/ Input/ Layout/ Symbols/ Emit/ Debug/ Transforms/ LTO/
│   └── MachO/                           → linkerMachO
│       └── Targets/ Driver/ Input/ Layout/ Symbols/ Emit/ Debug/ Transforms/ LTO/
│
└── Headers: neverc/include/neverc/Linker/{Core,COFF,ELF,MachO}/
```

Stable `Linker/<area>/<name>.h` include paths throughout; no relative
`"Foo.h"` includes. No cross-backend `#include`s.

---

## Library graph

```text
             ┌──────────────────────┐
             │     neverc (tool)    │
             └──────────┬───────────┘
         ┌──────────────┼──────────────┐
         ▼              ▼              ▼
   linkerCOFF      linkerELF      linkerMachO
         └──────────────┼──────────────┘
                        ▼
                   linkerCore → LLVM*
```

---

## Options

Each backend has an `Options.td.h` included from its driver files.
Many flags are forwarded by `neverc` through `LinkerDriverConfig`
(in `Linker/Core/Driver/Dispatcher.h`) instead of being parsed
by the linker — LTO tuning, threads, sysroot, gc-sections, ICF, etc.

COFF is a first-class target; do not remove without a deprecation plan.

---

## Building

```bash
cmake -S llvm -B build-neverc -G Ninja -C neverc/cmake/caches/NeverC.cmake
./build-neverc.sh
# Toggle: -DLINKER_ENABLE_{COFF,ELF,MACHO}=OFF
```

---

## Adding a new backend

1. Create `Backends/<Format>/` with `Options.td.h`, sources in
   buckets (`Driver/`, `Input/`, etc.), and a `CMakeLists.txt` calling
   `linker_declare_backend(NAME ...)`.
2. Add headers at `neverc/include/neverc/Linker/<Format>/`.
3. Append to `LINKER_BACKEND_DIRS`, add `LINKER_ENABLE_<UPPER>` option.
4. Register flavor in `Dispatcher.h`, wire up in `neverc/main.cpp`.

## Adding a new Core file

Drop source + header into the matching bucket (`Driver/`, `Runtime/`,
or `Support/`), then append to `linker_core_<bucket>` in
`Core/CMakeLists.txt`.
