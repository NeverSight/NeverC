#===----------------------------------------------------------------------===#
#
#  linkerCore — driver-agnostic foundation reused by every backend.
#
#  The sources live under three self-describing buckets; all three compile
#  into a single static library named `linkerCore`.  The split is purely
#  organisational — it lets the source tree describe *what* each file does
#  at a glance — not a CMake-level abstraction.  Drop a new file under the
#  bucket whose name best fits its responsibility and append it to the
#  matching list below.
#
#      Driver/    argv plumbing + codegen flags
#                   (ArgList, CodegenFlags)
#      Runtime/   per-link lifetime: session, allocator, diagnostics,
#                   stopwatch
#                   (Session, Allocator, Diagnostic, Stopwatch)
#      Support/   general-purpose helpers glued to the LLVM surface
#                   (Strings, FileIO, Dwarf, Chunks, LlvmAliases —
#                    the last two are header-only)
#
#===----------------------------------------------------------------------===#

# -----------------------------------------------------------------------------
# Source list, grouped by bucket.  IDEs pick the same buckets up as virtual
# folders through the `source_group` calls below.
# -----------------------------------------------------------------------------
set(linker_core_driver
  Driver/ArgList.cpp
  Driver/CodegenFlags.cpp
  Driver/CommonLTOConfig.cpp)

set(linker_core_runtime
  Runtime/Allocator.cpp
  Runtime/Diagnostic.cpp
  Runtime/Session.cpp
  Runtime/Stopwatch.cpp)

set(linker_core_support
  Support/Dwarf.cpp
  Support/FileIO.cpp
  Support/Strings.cpp)

source_group("Driver"  FILES ${linker_core_driver})
source_group("Runtime" FILES ${linker_core_runtime})
source_group("Support" FILES ${linker_core_support})

# -----------------------------------------------------------------------------
# Assemble `linkerCore`.
# -----------------------------------------------------------------------------
linker_add_library(linkerCore
  ${linker_core_driver}
  ${linker_core_runtime}
  ${linker_core_support}

  ADDITIONAL_HEADER_DIRS
  ${LINKER_INCLUDE_DIR}/linker/Core

  LINK_COMPONENTS
  Codegen
  Core
  DebugInfoDWARF
  MC
  Option
  Support
  Target
  TargetParser

  LINK_LIBS
  nevercEmit
  ${LLVM_PTHREAD_LIB}
  ${LLVM_ATOMIC_LIB}

  DEPENDS
  intrinsics_gen
)
