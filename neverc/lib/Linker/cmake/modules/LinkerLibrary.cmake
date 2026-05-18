#===----------------------------------------------------------------------===#
#
#  NeverC Linker — CMake helper module.
#
#  Every `CMakeLists.txt` in `neverc/lib/Linker/` (Core + each
#  `Backends/<Format>/`) goes through exactly one of
#  the two helpers declared in this file:
#
#      linker_add_library(<target> [SHARED] <llvm_add_library args>)
#          Static linker library factory.  Wraps `llvm_add_library` with
#          the install + export rules every linker target shares (folder
#          tag, install/export rules, LINKER_EXPORTS bookkeeping).
#
#      linker_declare_backend(NAME <tag>
#                             SOURCES <files...>
#                             [EXTRA_COMPONENTS <llvm components...>]
#                             [EXTRA_LIBS <libraries...>]
#                             [EXTRA_DEPS <dependencies...>])
#          Defines a backend library called `linker<tag>` (e.g.
#          `linkerCOFF`).  Links the shared LLVM component baseline plus
#          `linkerCore` automatically.
#
#  `linkerCore` itself is a plain `linker_add_library` call sitting in
#  `Core/CMakeLists.txt`; there is no per-bucket helper because the
#  three Core sub-folders (Driver, Runtime, Support) are organisational
#  buckets, not independent libraries.
#
#  Adding a new backend never requires editing this file.
#
#===----------------------------------------------------------------------===#

include_guard(GLOBAL)

include(GNUInstallDirs)
include(LLVMDistributionSupport)

# ---------------------------------------------------------------------------
# Shared backend baseline.
#
# Every object-format backend links the same set of LLVM components.  A new
# target architecture is registered here exactly once and is instantly
# available to every backend that calls `linker_declare_backend`.
# ---------------------------------------------------------------------------
set(LINKER_BACKEND_LLVM_COMPONENTS
  AArch64CodeGen AArch64AsmParser AArch64Desc AArch64Info AArch64Utils
  X86CodeGen X86AsmParser X86Desc X86Info
  BinaryFormat
  BitWriter
  Core
  DebugInfoDWARF
  LTO
  MC
  Object
  Option
  Passes
  Support
  TargetParser
  CACHE INTERNAL "LLVM components every linker backend links against")

set(LINKER_BACKEND_BASE_LIBS
  linkerCore
  ${LLVM_PTHREAD_LIB}
  CACHE INTERNAL "Libraries implicitly linked into every linker backend")

# ---------------------------------------------------------------------------
# linker_add_library(<target> [SHARED] <llvm_add_library args>)
#
# Thin wrapper around `llvm_add_library`.  Responsibilities:
#   - tag the target under the "neverc linker libraries" IDE folder,
#   - attach install + export rules,
#   - register the target under the global LINKER_EXPORTS property so it
#     can be re-exported from the top-level install tree.
# ---------------------------------------------------------------------------
function(linker_add_library target_name)
  cmake_parse_arguments(_LIB "SHARED" "" "" ${ARGN})

  set(_lib_kind "")
  if(_LIB_SHARED)
    set(_lib_kind "SHARED")
  endif()

  llvm_add_library(${target_name} ${_lib_kind} ${_LIB_UNPARSED_ARGUMENTS})
  set_target_properties(${target_name} PROPERTIES FOLDER "neverc linker libraries")

  if(LLVM_INSTALL_TOOLCHAIN_ONLY)
    return()
  endif()

  get_target_export_arg(${target_name} Linker _export_arg)
  install(TARGETS ${target_name}
    COMPONENT ${target_name}
    ${_export_arg}
    LIBRARY DESTINATION lib${LLVM_LIBDIR_SUFFIX}
    ARCHIVE DESTINATION lib${LLVM_LIBDIR_SUFFIX}
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

  if(_LIB_SHARED AND NOT CMAKE_CONFIGURATION_TYPES)
    add_llvm_install_targets(install-${target_name}
      DEPENDS ${target_name}
      COMPONENT ${target_name})
  endif()

  set_property(GLOBAL APPEND PROPERTY LINKER_EXPORTS ${target_name})
endfunction()

# ---------------------------------------------------------------------------
# linker_declare_backend(NAME <COFF|ELF|MachO|...>
#                        SOURCES <files...>
#                        [EXTRA_COMPONENTS <llvm components...>]
#                        [EXTRA_LIBS <libraries...>]
#                        [EXTRA_DEPS <dependencies...>])
#
# Declares a backend library called `linker<NAME>` (e.g. `linkerCOFF`).
#
# The resulting library always links `linkerCore` plus
# `LINKER_BACKEND_LLVM_COMPONENTS`; callers only list the backend-specific
# delta.
# ---------------------------------------------------------------------------
function(linker_declare_backend)
  set(_one_value_args NAME)
  set(_multi_value_args SOURCES EXTRA_COMPONENTS EXTRA_LIBS EXTRA_DEPS)
  cmake_parse_arguments(_BE "" "${_one_value_args}" "${_multi_value_args}" ${ARGN})

  if(NOT _BE_NAME)
    message(FATAL_ERROR "linker_declare_backend: NAME is required")
  endif()
  if(NOT _BE_SOURCES)
    message(FATAL_ERROR
      "linker_declare_backend(${_BE_NAME}): SOURCES is required")
  endif()

  set(_target "linker${_BE_NAME}")
  set(_tablegen_target "${_BE_NAME}OptionsTableGen")
  set(_public_header_dir "${LINKER_INCLUDE_DIR}/Linker/${_BE_NAME}")

  add_custom_target(${_tablegen_target})

  linker_add_library(${_target}
    ${_BE_SOURCES}

    ADDITIONAL_HEADER_DIRS
    ${_public_header_dir}

    LINK_COMPONENTS
    ${LINKER_BACKEND_LLVM_COMPONENTS}
    ${_BE_EXTRA_COMPONENTS}

    LINK_LIBS
    ${LINKER_BACKEND_BASE_LIBS}
    ${_BE_EXTRA_LIBS}

    DEPENDS
    ${_tablegen_target}
    intrinsics_gen
    ${_BE_EXTRA_DEPS}
  )
endfunction()
