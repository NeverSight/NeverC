# NeverC unified compiler + linker CMake cache
# Use: cmake -S llvm -B build-neverc -C neverc/cmake/caches/NeverC.cmake
set(CMAKE_BUILD_TYPE Release CACHE STRING "")
set(NEVERC_RELEASE_OPT_LEVEL "2" CACHE STRING
    "Release optimization level for building neverc itself (2 or 3)")
if(NOT CMAKE_HOST_WIN32)
  if(NEVERC_RELEASE_OPT_LEVEL STREQUAL "3")
    set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG" CACHE STRING "" FORCE)
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG" CACHE STRING "" FORCE)
  else()
    set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG" CACHE STRING "" FORCE)
    set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG" CACHE STRING "" FORCE)
  endif()
endif()

# When building on the same architecture we're targeting, enable
# native microarchitecture tuning (-march=native) for the compiler
# binary itself.  Disable with -DNEVERC_NATIVE_ARCH=OFF for portable
# release builds.
option(NEVERC_NATIVE_ARCH "Enable -march=native for local builds" ON)
if(NEVERC_NATIVE_ARCH AND NOT CMAKE_CROSSCOMPILING AND NOT MSVC)
  set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -march=native" CACHE STRING "" FORCE)
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -march=native" CACHE STRING "" FORCE)
endif()
# Only neverc is a standalone LLVM "project" now; the linker lives as an
# internal subdirectory (neverc/lib/Linker) that neverc consumes directly.
set(LLVM_ENABLE_PROJECTS "neverc" CACHE STRING "")
set(LLVM_TARGETS_TO_BUILD "AArch64;X86" CACHE STRING "" FORCE)

# Exclude tests/examples/benchmarks/utils.
set(LLVM_INCLUDE_TESTS OFF CACHE BOOL "")
set(LLVM_BUILD_TESTS OFF CACHE BOOL "")
set(LLVM_INCLUDE_EXAMPLES OFF CACHE BOOL "")
set(LLVM_BUILD_EXAMPLES OFF CACHE BOOL "")
set(LLVM_INCLUDE_BENCHMARKS OFF CACHE BOOL "")
set(LLVM_BUILD_BENCHMARKS OFF CACHE BOOL "")
set(LLVM_BUILD_UTILS OFF CACHE BOOL "")
set(LLVM_INCLUDE_TOOLS OFF CACHE BOOL "")
set(LLVM_BUILD_TOOLS OFF CACHE BOOL "")
set(NEVERC_BUILD_TOOLS ON CACHE BOOL "")
set(NEVERC_INCLUDE_TESTS OFF CACHE BOOL "")
set(LLVM_INSTALL_TOOLCHAIN_ONLY ON CACHE BOOL "")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "")
set(LLVM_LINK_LLVM_DYLIB OFF CACHE BOOL "")
set(LLVM_BUILD_LLVM_DYLIB OFF CACHE BOOL "")
set(LLVM_BUILD_LLVM_C_DYLIB OFF CACHE BOOL "")

# Default to lld on every platform when available.  LLVM_ENABLE_LLD wires
# -fuse-ld=lld on Unix/macOS and CMAKE_LINKER=lld-link on MSVC, so Full LTO
# works without LLVMgold.so.  Linux/Windows always opt in; macOS falls back to
# ld64 only when the host toolchain has no lld (e.g. bare Xcode clang).
if(NOT CMAKE_CROSSCOMPILING)
  set(_NEVERC_USE_LLD FALSE)
  if(MSVC)
    set(_NEVERC_USE_LLD TRUE)
  else()
    get_filename_component(_NEVERC_CXX_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    find_program(_NEVERC_LLD NAMES ld64.lld ld.lld lld
      HINTS "${_NEVERC_CXX_DIR}" NO_DEFAULT_PATH)
    if(NOT _NEVERC_LLD)
      find_program(_NEVERC_LLD NAMES ld64.lld ld.lld lld)
    endif()
    if(_NEVERC_LLD OR NOT APPLE)
      set(_NEVERC_USE_LLD TRUE)
    endif()
  endif()
  if(_NEVERC_USE_LLD)
    set(LLVM_ENABLE_LLD ON CACHE BOOL "" FORCE)
  endif()
endif()

# Official LLVM release clang on macOS emits object files that Xcode libtool
# cannot archive (Producer: LLVM22.x vs Reader: Apple libtool).  Use llvm-ar from
# the same toolchain when the host compiler is not Apple clang.
if(CMAKE_HOST_APPLE AND CMAKE_CXX_COMPILER)
  execute_process(
    COMMAND "${CMAKE_CXX_COMPILER}" --version
    OUTPUT_VARIABLE _NEVERC_CLANG_VERSION
    ERROR_VARIABLE _NEVERC_CLANG_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  if(_NEVERC_CLANG_VERSION MATCHES "clang version"
     AND NOT _NEVERC_CLANG_VERSION MATCHES "Apple")
    set(NEVERC_USE_LLVM_AR ON CACHE BOOL
        "Use llvm-ar instead of Xcode libtool for static libraries" FORCE)
    get_filename_component(_NEVERC_COMPILER_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    find_program(_NEVERC_LLVM_AR NAMES llvm-ar
      HINTS "${_NEVERC_COMPILER_DIR}" NO_DEFAULT_PATH)
    find_program(_NEVERC_LLVM_RANLIB NAMES llvm-ranlib
      HINTS "${_NEVERC_COMPILER_DIR}" NO_DEFAULT_PATH)
    if(_NEVERC_LLVM_AR)
      set(CMAKE_AR "${_NEVERC_LLVM_AR}" CACHE FILEPATH "" FORCE)
    endif()
    if(_NEVERC_LLVM_RANLIB)
      set(CMAKE_RANLIB "${_NEVERC_LLVM_RANLIB}" CACHE FILEPATH "" FORCE)
    endif()
  endif()
endif()

# The release artifact is a single neverc executable linked from static
# libraries, so avoid PIC/unwind/debugging extras and the associated configure
# probes by default.
set(LLVM_ENABLE_PIC OFF CACHE BOOL "")
set(LLVM_ENABLE_UNWIND_TABLES OFF CACHE BOOL "")
set(LLVM_ENABLE_CRASH_OVERRIDES OFF CACHE BOOL "")
set(LLVM_VERSION_PRINTER_SHOW_HOST_TARGET_INFO OFF CACHE BOOL "")
set(LLVM_ENABLE_SUPPORT_XCODE_SIGNPOSTS FORCE_OFF CACHE STRING "")
set(LLVM_ENABLE_WARNINGS OFF CACHE BOOL "")
set(LLVM_ENABLE_THREADS ON CACHE BOOL "" FORCE)
set(LLVM_ENABLE_ASSERTIONS OFF CACHE BOOL "")
set(LLVM_ENABLE_DUMP OFF CACHE BOOL "")
set(LLVM_ENABLE_PROC_RUSAGE_PROBE OFF CACHE BOOL "")
set(LLVM_ENABLE_MALLOC_USAGE_PROBES OFF CACHE BOOL "")

# NeverC Linker backends (COFF / ELF / Mach-O) enabled by default.
set(LINKER_ENABLE_MACHO ON CACHE BOOL "")
set(LINKER_ENABLE_ELF ON CACHE BOOL "")
set(LINKER_ENABLE_COFF ON CACHE BOOL "")

set(LLVM_ENABLE_PLUGINS OFF CACHE BOOL "")
set(NEVERC_PLUGIN_SUPPORT OFF CACHE BOOL "")
set(LLVM_ENABLE_TERMINFO OFF CACHE BOOL "")
set(LLVM_ENABLE_LIBEDIT OFF CACHE BOOL "")
set(LLVM_ENABLE_ZLIB OFF CACHE STRING "")
set(LLVM_ENABLE_ZSTD OFF CACHE STRING "")
set(LLVM_ENABLE_BACKTRACES OFF CACHE BOOL "")
set(LLVM_APPEND_VC_REV OFF CACHE BOOL "")
set(NEVERC_DETECT_HOST_LINK_VERSION OFF CACHE BOOL "")
set(NEVERC_ENABLE_ORDER_FILE OFF CACHE BOOL "")
set(NEVERC_ENABLE_MIMALLOC ON CACHE BOOL "")
set(NEVERC_STRIP_BINARY ON CACHE BOOL "")

# Full LTO for the final neverc binary: interprocedural optimisation.
# Disabled under MSVC (uses /LTCG instead) and when cross-compiling
# (linker may not support it).
option(NEVERC_ENABLE_LTO "Enable Full LTO for the neverc binary" ON)
if(NEVERC_ENABLE_LTO AND NOT CMAKE_CROSSCOMPILING AND NOT MSVC)
  set(LLVM_ENABLE_LTO Full CACHE STRING "" FORCE)
endif()

# Profile-Guided Optimisation (PGO) two-phase build.
#
# NOTE: when using -C (cache preload), conditional logic runs before -D
# overrides take effect.  Pass PGO flags explicitly on the command line:
#
#   Phase 1 (generate):
#     cmake -S llvm -B build -G Ninja -C neverc/cmake/caches/NeverC.cmake \
#       -DNEVERC_RELEASE_OPT_LEVEL=3 \
#       -DCMAKE_CXX_FLAGS_RELEASE="-O3 -DNDEBUG -march=native -fprofile-instr-generate -ffunction-sections -fdata-sections" \
#       -DCMAKE_C_FLAGS_RELEASE="-O3 -DNDEBUG -march=native -fprofile-instr-generate -ffunction-sections -fdata-sections" \
#       -DCMAKE_EXE_LINKER_FLAGS="-fprofile-instr-generate"
#     ninja -C build neverc
#
#   Phase 1b (train with ALL compilation modes for full pipeline coverage):
#     LLVM_PROFILE_FILE=train_sema.profraw    build/bin/neverc -fsyntax-only -w <workload.c>
#     LLVM_PROFILE_FILE=train_irgen.profraw   build/bin/neverc -emit-llvm -S -w -o /dev/null <workload.c>
#     LLVM_PROFILE_FILE=train_compile.profraw build/bin/neverc -c -w -o /dev/null <workload.c>
#     LLVM_PROFILE_FILE=train_preproc.profraw build/bin/neverc -E -w -o /dev/null <workload.c>
#     xcrun llvm-profdata merge -output=default.profdata train_*.profraw
#
#   Phase 2 (use):
#     cmake -S llvm -B build -G Ninja -C neverc/cmake/caches/NeverC.cmake \
#       -DNEVERC_RELEASE_OPT_LEVEL=3 \
#       -DCMAKE_CXX_FLAGS_RELEASE="-O3 -DNDEBUG -march=native -fprofile-instr-use=$PWD/default.profdata -ffunction-sections -fdata-sections" \
#       -DCMAKE_C_FLAGS_RELEASE="-O3 -DNDEBUG -march=native -fprofile-instr-use=$PWD/default.profdata -ffunction-sections -fdata-sections" \
#       -DCMAKE_EXE_LINKER_FLAGS="-Wl,-dead_strip"
#     ninja -C build neverc
set(NEVERC_PGO_MODE "OFF" CACHE STRING
    "PGO mode: OFF (default), generate, or use")
set(NEVERC_PGO_PROFILE "" CACHE FILEPATH
    "Path to merged .profdata file (required when NEVERC_PGO_MODE=use)")
if(NOT MSVC)
  if(NEVERC_PGO_MODE STREQUAL "generate")
    set(CMAKE_C_FLAGS_RELEASE
        "${CMAKE_C_FLAGS_RELEASE} -fprofile-instr-generate -DNEVERC_PGO_TRAINING"
        CACHE STRING "" FORCE)
    set(CMAKE_CXX_FLAGS_RELEASE
        "${CMAKE_CXX_FLAGS_RELEASE} -fprofile-instr-generate -DNEVERC_PGO_TRAINING"
        CACHE STRING "" FORCE)
    set(CMAKE_EXE_LINKER_FLAGS
        "${CMAKE_EXE_LINKER_FLAGS} -fprofile-instr-generate"
        CACHE STRING "" FORCE)
  elseif(NEVERC_PGO_MODE STREQUAL "use")
    if(NOT NEVERC_PGO_PROFILE)
      message(FATAL_ERROR
        "NEVERC_PGO_MODE=use requires NEVERC_PGO_PROFILE to be set")
    endif()
    set(CMAKE_C_FLAGS_RELEASE
        "${CMAKE_C_FLAGS_RELEASE} -fprofile-instr-use=${NEVERC_PGO_PROFILE}"
        CACHE STRING "" FORCE)
    set(CMAKE_CXX_FLAGS_RELEASE
        "${CMAKE_CXX_FLAGS_RELEASE} -fprofile-instr-use=${NEVERC_PGO_PROFILE}"
        CACHE STRING "" FORCE)
  endif()
endif()

# Section-level GC: compile each function / global into its own section so
# the linker can discard unreferenced code and data.  Reduces binary size
# and improves I-cache utilisation.
# Disabled during PGO generate: -dead_strip / --gc-sections removes the
# __llvm_prf* profiling data sections, preventing profile output.
if(NOT MSVC)
  set(CMAKE_C_FLAGS_RELEASE
      "${CMAKE_C_FLAGS_RELEASE} -ffunction-sections -fdata-sections"
      CACHE STRING "" FORCE)
  set(CMAKE_CXX_FLAGS_RELEASE
      "${CMAKE_CXX_FLAGS_RELEASE} -ffunction-sections -fdata-sections"
      CACHE STRING "" FORCE)
  if(NOT NEVERC_PGO_MODE STREQUAL "generate")
    if(APPLE)
      set(NEVERC_GC_LINKER_FLAG "-Wl,-dead_strip")
    else()
      set(NEVERC_GC_LINKER_FLAG "-Wl,--gc-sections")
    endif()
    if(NOT CMAKE_EXE_LINKER_FLAGS MATCHES "${NEVERC_GC_LINKER_FLAG}")
      set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${NEVERC_GC_LINKER_FLAG}"
          CACHE STRING "" FORCE)
    endif()
  endif()
endif()

find_program(NEVERC_CCACHE_PROG NAMES ccache sccache)
if(NEVERC_CCACHE_PROG)
  set(LLVM_CCACHE_BUILD ON CACHE BOOL "")
  set(LLVM_CCACHE_PROGRAM "${NEVERC_CCACHE_PROG}" CACHE STRING "")
  message(STATUS "NeverC: enabling compiler cache via ${NEVERC_CCACHE_PROG}")
endif()

if(NOT MSVC)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-function" CACHE STRING "" FORCE)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-function" CACHE STRING "" FORCE)
endif()
