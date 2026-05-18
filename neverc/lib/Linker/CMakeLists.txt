#===----------------------------------------------------------------------===#
#
#  NeverC Linker — top-level CMake entry point.
#
#  The NeverC linker is distributed as a set of
#  static libraries (linkerCore + one library per object format) linked
#  into the unified `neverc` executable.  There is no standalone linker
#  binary: `neverc` selects a flavor (`linker::Flavor`) and calls the
#  matching backend's `link()` directly through a `DriverDef[]` table.
#
#  This file stays intentionally small.  It only wires up paths, options
#  and the backend registry; the heavy lifting (library factory, shared
#  LLVM component baseline) lives in `cmake/modules/LinkerLibrary.cmake`.
#
#===----------------------------------------------------------------------===#

cmake_minimum_required(VERSION 3.20.0)

# -----------------------------------------------------------------------------
# Reject standalone configuration.  The linker only makes sense as part of
# the larger neverc/LLVM tree.
# -----------------------------------------------------------------------------
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  message(FATAL_ERROR
    "neverc/Linker cannot be configured on its own; build via the top-level "
    "LLVM tree with `cmake -S llvm -B build-neverc -C neverc/cmake/caches/NeverC.cmake`.")
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR
    "In-source builds are not allowed. Please create a separate build "
    "directory and invoke cmake from there.")
endif()

# -----------------------------------------------------------------------------
# Canonical paths.  Backends reference these to find their public headers.
# -----------------------------------------------------------------------------
set(LINKER_SOURCE_DIR  ${CMAKE_CURRENT_SOURCE_DIR})
set(LINKER_INCLUDE_DIR ${NEVERC_SOURCE_DIR}/include/neverc)
set(LINKER_BINARY_DIR  ${CMAKE_CURRENT_BINARY_DIR})

# -----------------------------------------------------------------------------
# CMake module path.
# -----------------------------------------------------------------------------
include(GNUInstallDirs)

list(INSERT CMAKE_MODULE_PATH 0
  "${LINKER_SOURCE_DIR}/cmake/modules"
  "${LLVM_COMMON_CMAKE_UTILS}/Modules")

include(LinkerLibrary)

# -----------------------------------------------------------------------------
# Platform-specific compile fixups.
# -----------------------------------------------------------------------------
if(MSVC)
  add_definitions(-wd4530) # C4530: C++ exception handler used, but unwind semantics are not enabled.
  add_definitions(-wd4062) # C4062: enumerator X in switch of enum Y is not handled
endif()

# Every translation unit reaches headers through `Linker/<area>/...`; register
# both the source and the build `include/` roots so generated files
# (Options.inc) resolve uniformly.
include_directories(BEFORE
  ${LINKER_BINARY_DIR}/include
  ${LINKER_INCLUDE_DIR})

if(NOT LLVM_INSTALL_TOOLCHAIN_ONLY)
  install(DIRECTORY ${LINKER_INCLUDE_DIR}/Linker/
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/neverc"
    FILES_MATCHING PATTERN "*.h")
endif()

# -----------------------------------------------------------------------------
# Backend registry.
#
# Adding a new object format is a three-step drop-in:
#   1. drop its sources under `neverc/lib/Linker/Backends/<Directory>/`
#   2. provide a `CMakeLists.txt` that calls
#      `linker_declare_backend(NAME ...)`
#   3. append the directory to `LINKER_BACKEND_DIRS` below and declare a
#      matching `LINKER_ENABLE_<UPPER>` option (defaults to ON).
#
# Turning a backend off removes both the CMake inclusion and the resulting
# static library from the `neverc` link line.
# -----------------------------------------------------------------------------
option(LINKER_ENABLE_COFF  "Build COFF linker backend (linkerCOFF)."    ON)
option(LINKER_ENABLE_ELF   "Build ELF linker backend (linkerELF)."      ON)
option(LINKER_ENABLE_MACHO "Build Mach-O linker backend (linkerMachO)." ON)

# Logical backend tags (COFF / ELF / MachO).  Sources live under
# `Backends/<tag>/`; `LINKER_ENABLE_<TAG>` options key off the tag only.
set(LINKER_BACKEND_DIRS COFF ELF MachO)

# linkerCore is mandatory; every backend links against it.
add_subdirectory(Core)

set(LINKER_ENABLED_BACKEND_LIBS)
foreach(backend IN LISTS LINKER_BACKEND_DIRS)
  string(TOUPPER "${backend}" backend_upper)
  set(opt_var "LINKER_ENABLE_${backend_upper}")
  if(${${opt_var}})
    message(STATUS "NeverC Linker: enabling ${backend} backend")
    add_subdirectory(Backends/${backend})
    list(APPEND LINKER_ENABLED_BACKEND_LIBS "linker${backend}")
  else()
    message(STATUS "NeverC Linker: skipping ${backend} backend (${opt_var}=OFF)")
  endif()
endforeach()

set(LINKER_ENABLED_BACKEND_LIBS "${LINKER_ENABLED_BACKEND_LIBS}" PARENT_SCOPE)
