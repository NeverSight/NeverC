#===----------------------------------------------------------------------===#
#
#  linkerCOFF — Windows PE/COFF backend.
#
#  All the plumbing (`linkerCore` dependency, shared LLVM components)
#  lives in `linker_declare_backend`.  Callers just supply the sources
#  plus any backend-specific LLVM components or platform libs.
#
#===----------------------------------------------------------------------===#

linker_declare_backend(
  NAME COFF

  SOURCES
    # Command-line parsing and the COFF entry point.
    Driver/CoffCommandLine.cpp
    Driver/CoffDriver.cpp

    # Input ingestion and link-time code generation.
    Input/InputFiles.cpp
    LTO/LTO.cpp

    # Output layout primitives.
    Layout/Chunks.cpp
    Layout/DLL.cpp

    # Final image and map-file emission.
    Emit/CoffImageEmitter.cpp
    Emit/CoffLinkMapEmitter.cpp

    # Reachability and folding passes.
    Transforms/CallGraphSort.cpp
    Transforms/ICF.cpp
    Transforms/MarkLive.cpp

    # Per-link state and symbols.
    State/COFFLinkerContext.cpp
    Symbols/SymbolTable.cpp
    Symbols/Symbols.cpp

  EXTRA_COMPONENTS

  EXTRA_LIBS
    ${LLVM_ATOMIC_LIB}
)
