#===----------------------------------------------------------------------===#
#
#  linkerMachO — Mach-O backend.
#
#  All the plumbing (`linkerCore` dependency, shared LLVM components)
#  lives in `linker_declare_backend`.  Callers just supply the sources
#  + backend-specific LLVM components.
#
#  Note: Mach-O needs mach-o/compact_unwind_encoding.h to parse compact unwind
#  sections.  Prefer libunwind when present (full llvm-project checkout);
#  otherwise use the vendored copy under include/ so Linux CI can build the
#  Mach-O backend without the macOS SDK.
#
#===----------------------------------------------------------------------===#

set(_NEVERC_MACHO_UNWIND_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/include")
if(EXISTS "${LLVM_MAIN_SRC_DIR}/../libunwind/include/mach-o/compact_unwind_encoding.h")
  set(_NEVERC_MACHO_UNWIND_INCLUDE "${LLVM_MAIN_SRC_DIR}/../libunwind/include")
endif()
include_directories(${_NEVERC_MACHO_UNWIND_INCLUDE})

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

    # Task-local backend ownership.
    State/MachOLinkerContext.cpp

    # NeverC C-ABI LinkGraph, phase, and BinaryImage adapters. These sources
    # are intentionally compiled into the native backend, not the generic
    # plugin library, so the dependency remains one-way.
    ${NEVERC_SOURCE_DIR}/lib/Plugin/Link/MachO/MachOLinkGraphAdapter.cpp
    ${NEVERC_SOURCE_DIR}/lib/Plugin/Link/MachO/MachOPhaseProviders.cpp
    ${NEVERC_SOURCE_DIR}/lib/Plugin/Link/MachO/MachOBinaryImageAdapter.cpp

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
    nevercPluginLink
)

target_include_directories(linkerMachO PRIVATE
  ${NEVERC_SOURCE_DIR}/lib/Plugin
  ${NEVERC_SOURCE_DIR}/lib/Plugin/Link
)

# The Mach-O public headers (e.g. Target.h) include
# <mach-o/compact_unwind_encoding.h>. The directory-scoped include_directories()
# above only covers this backend's own sources, so out-of-tree consumers that
# include those headers (e.g. neverc-plugin-link-tests) fail to find the vendored
# copy on Linux/Windows. Export the search path on the target interface so any
# consumer linking linkerMachO inherits it.
target_include_directories(linkerMachO PUBLIC ${_NEVERC_MACHO_UNWIND_INCLUDE})
