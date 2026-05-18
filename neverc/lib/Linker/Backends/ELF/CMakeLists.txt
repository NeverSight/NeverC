#===----------------------------------------------------------------------===#
#
#  linkerELF — ELF backend.
#
#  All the plumbing (`linkerCore` dependency, shared LLVM components)
#  lives in `linker_declare_backend`.  Callers just supply the sources,
#  any backend-specific LLVM components and any optional third-party
#  libraries detected at configure time (zlib / zstd).
#
#===----------------------------------------------------------------------===#

# Optional compression libraries.  Only linked when the enclosing build
# opted into them; keeping the detection local keeps the top-level file
# clean.
set(linker_elf_optional_libs)
if(LLVM_ENABLE_ZLIB)
  list(APPEND linker_elf_optional_libs ZLIB::ZLIB)
endif()
if(LLVM_ENABLE_ZSTD)
  if(TARGET zstd::libzstd_shared AND NOT LLVM_USE_STATIC_ZSTD)
    list(APPEND linker_elf_optional_libs zstd::libzstd_shared)
  else()
    list(APPEND linker_elf_optional_libs zstd::libzstd_static)
  endif()
endif()

linker_declare_backend(
  NAME ELF

  SOURCES
    # Target-specific relocation and thunk support.
    Targets/AArch64.cpp
    Targets/X86_64.cpp

    # Debug and unwind metadata.
    Debug/DWARF.cpp
    Debug/EhFrame.cpp

    # Command-line parsing and the ELF entry point.
    Driver/ElfCommandLine.cpp
    Driver/ElfDriver.cpp

    # Input ingestion and link-time code generation.
    Input/InputFiles.cpp
    Input/InputSection.cpp
    LTO/LTO.cpp

    # Output layout, scripts, sections and relocations.
    Layout/LinkerScript.cpp
    Layout/OutputSections.cpp
    Layout/Relocations.cpp
    Layout/ScriptLexer.cpp
    Layout/ScriptParser.cpp
    Layout/SyntheticSections.cpp
    Layout/Target.cpp
    Layout/Thunks.cpp

    # Final image and map-file emission.
    Emit/ElfImageEmitter.cpp
    Emit/ElfLinkMapEmitter.cpp

    # Reachability, folding and layout fixes.
    Transforms/AArch64ErrataFix.cpp
    Transforms/CallGraphSort.cpp
    Transforms/ICF.cpp
    Transforms/MarkLive.cpp

    # Symbol records and lookup tables.
    Symbols/SymbolTable.cpp
    Symbols/Symbols.cpp

  EXTRA_COMPONENTS
    TransformUtils

  EXTRA_LIBS
    nevercMerge
    ${linker_elf_optional_libs}
)
