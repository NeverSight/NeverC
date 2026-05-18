#===----------------------------------------------------------------------===#
#
#  linkerMachO — Mach-O backend.
#
#  All the plumbing (`linkerCore` dependency, shared LLVM components)
#  lives in `linker_declare_backend`.  Callers just supply the sources
#  + backend-specific LLVM components.
#
#  Note: Mach-O needs libunwind's public headers to parse compact unwind
#  sections; expose them via `include_directories` before declaring the
#  backend so the generated target picks them up.
#
#===----------------------------------------------------------------------===#

include_directories(${LLVM_MAIN_SRC_DIR}/../libunwind/include)

linker_declare_backend(
  NAME MachO

  SOURCES
    # Target-specific relocation and thunk support.
    Targets/ARM64.cpp
    Targets/ARM64Common.cpp
    Targets/X86_64.cpp

    # Debug and unwind metadata.
    Debug/Dwarf.cpp
    Debug/EhFrame.cpp
    Debug/UnwindInfoSection.cpp

    # Command-line parsing and the Mach-O entry point.
    Driver/MachOCommandLine.cpp
    Driver/MachODriver.cpp

    # Input ingestion and link-time code generation.
    Input/InputFiles.cpp
    Input/InputSection.cpp
    LTO/LTO.cpp

    # Output sections, segments and relocations.
    Layout/ConcatOutputSection.cpp
    Layout/OutputSection.cpp
    Layout/OutputSegment.cpp
    Layout/Relocations.cpp
    Layout/SectionPriorities.cpp
    Layout/SyntheticSections.cpp
    Layout/Target.cpp

    # Reachability and folding passes.
    Transforms/ICF.cpp
    Transforms/MarkLive.cpp

    # Symbol records, lookup tables and export trie.
    Symbols/ExportTrie.cpp
    Symbols/SymbolTable.cpp
    Symbols/Symbols.cpp

    # Final image and map-file emission.
    Emit/MachOImageEmitter.cpp
    Emit/MachOLinkMapEmitter.cpp

  EXTRA_COMPONENTS
    BitReader
    TextAPI

  EXTRA_LIBS
    nevercMerge
)
