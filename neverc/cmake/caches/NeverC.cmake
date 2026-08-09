# NeverC unified compiler + linker CMake cache
# Use: cmake -S llvm -B build-neverc -C neverc/cmake/caches/NeverC.cmake
set(CMAKE_BUILD_TYPE Release CACHE STRING "")

# On macOS, prefer Homebrew LLVM toolchain so the compiler and linker
# (lld) share the same LLVM version.  Mixing Apple Clang with Homebrew
# LLD causes LTO crashes due to bitcode incompatibilities.
# Override with: -DCMAKE_C_COMPILER=... -DCMAKE_CXX_COMPILER=...
if(CMAKE_HOST_APPLE AND NOT DEFINED CMAKE_C_COMPILER)
  find_program(_NEVERC_BREW_CC  clang   PATHS /opt/homebrew/opt/llvm/bin NO_DEFAULT_PATH)
  find_program(_NEVERC_BREW_CXX clang++ PATHS /opt/homebrew/opt/llvm/bin NO_DEFAULT_PATH)
  if(_NEVERC_BREW_CC AND _NEVERC_BREW_CXX)
    set(CMAKE_C_COMPILER   "${_NEVERC_BREW_CC}"  CACHE FILEPATH "")
    set(CMAKE_CXX_COMPILER "${_NEVERC_BREW_CXX}" CACHE FILEPATH "")
    message(STATUS "NeverC: using Homebrew LLVM toolchain (${_NEVERC_BREW_CXX})")
  endif()
endif()
# Detect MSVC early: in cache preload (-C), MSVC is not yet set (project()
# hasn't run).  Use CMAKE_HOST_WIN32 + absence of explicit Clang compiler
# as proxy.  On Windows with no -DCMAKE_C_COMPILER override, assume MSVC.
set(_NEVERC_HOST_MSVC FALSE)
if(CMAKE_HOST_WIN32 AND NOT DEFINED CMAKE_C_COMPILER)
  set(_NEVERC_HOST_MSVC TRUE)
elseif(DEFINED CMAKE_C_COMPILER AND CMAKE_C_COMPILER MATCHES "[/\\\\]cl\\.exe$")
  set(_NEVERC_HOST_MSVC TRUE)
endif()

set(NEVERC_RELEASE_OPT_LEVEL "2" CACHE STRING
    "Release optimization level for building neverc itself (2 or 3)")
if(NOT _NEVERC_HOST_MSVC)
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
if(NEVERC_NATIVE_ARCH AND NOT CMAKE_CROSSCOMPILING AND NOT _NEVERC_HOST_MSVC)
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
# works without LLVMgold.so.  Opt in only when the matching lld is actually
# present; otherwise fall back to the platform default linker (MSVC link.exe,
# or ld64 on bare Xcode clang).
if(NOT CMAKE_CROSSCOMPILING)
  set(_NEVERC_USE_LLD FALSE)
  # find_program() caches its result by default.  Re-resolve the linker on
  # every configure so switching compilers cannot retain an incompatible lld.
  unset(_NEVERC_LLD)
  unset(_NEVERC_LLD CACHE)
  if(_NEVERC_HOST_MSVC)
    # On MSVC, LLD is the separate lld-link.exe, which ships with LLVM/clang
    # rather than the MSVC toolset.  Only opt in when it is on PATH; otherwise
    # keep LLVM_ENABLE_LLD off and let the build use the MSVC linker (link.exe).
    find_program(_NEVERC_LLD NAMES lld-link)
    if(_NEVERC_LLD)
      set(_NEVERC_USE_LLD TRUE)
    endif()
  else()
    get_filename_component(_NEVERC_CXX_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    find_program(_NEVERC_LLD NAMES ld64.lld ld.lld lld
      HINTS "${_NEVERC_CXX_DIR}" NO_DEFAULT_PATH)

    # Never mix Apple Clang bitcode with a Homebrew LLD found elsewhere on
    # PATH.  Their LLVM implementations are versioned independently, and the
    # mismatch can crash ThinLTO instead of producing a normal linker error.
    # A non-Apple Clang may use the separately packaged Homebrew lld formula;
    # Homebrew keeps that formula on the same LLVM release as its clang.
    set(_NEVERC_APPLE_CLANG FALSE)
    if(CMAKE_HOST_APPLE)
      if(CMAKE_CXX_COMPILER)
        execute_process(
          COMMAND "${CMAKE_CXX_COMPILER}" --version
          OUTPUT_VARIABLE _NEVERC_CXX_VERSION
          ERROR_VARIABLE _NEVERC_CXX_VERSION
          OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(_NEVERC_CXX_VERSION MATCHES "Apple clang")
          set(_NEVERC_APPLE_CLANG TRUE)
        endif()
      else()
        # With no override, CMake will select Xcode's Apple Clang later in
        # project().  Do not pair that implicit compiler with a PATH lld.
        set(_NEVERC_APPLE_CLANG TRUE)
      endif()
    endif()

    if(NOT _NEVERC_LLD AND NOT _NEVERC_APPLE_CLANG)
      find_program(_NEVERC_LLD NAMES ld64.lld ld.lld lld)
    endif()
    if(_NEVERC_LLD OR NOT CMAKE_HOST_APPLE)
      set(_NEVERC_USE_LLD TRUE)
    endif()
  endif()
  if(_NEVERC_USE_LLD)
    set(LLVM_ENABLE_LLD ON CACHE BOOL "" FORCE)
  elseif(CMAKE_HOST_APPLE)
    # Clear a stale value when a build directory switches back to Apple Clang.
    set(LLVM_ENABLE_LLD OFF CACHE BOOL "" FORCE)
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
# Static CRT (/MT) for standalone deployment and plugin compatibility.
if(_NEVERC_HOST_MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "" FORCE)
endif()

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
set(NEVERC_ENABLE_PYTHON_PLUGINS ON CACHE BOOL "")
set(NEVERC_BUNDLE_PYTHON_RUNTIME ON CACHE BOOL "")
set(LLVM_ENABLE_TERMINFO OFF CACHE BOOL "")
set(LLVM_ENABLE_LIBEDIT OFF CACHE BOOL "")
set(LLVM_ENABLE_ZLIB FORCE_ON CACHE STRING "")
set(LLVM_ENABLE_ZSTD FORCE_ON CACHE STRING "")
set(LLVM_USE_BUNDLED_ZLIB ON CACHE BOOL "")
set(LLVM_USE_BUNDLED_ZSTD ON CACHE BOOL "")
set(LLVM_USE_STATIC_ZSTD ON CACHE BOOL "")
set(LLVM_ENABLE_BACKTRACES OFF CACHE BOOL "")
set(LLVM_APPEND_VC_REV OFF CACHE BOOL "")
set(NEVERC_DETECT_HOST_LINK_VERSION OFF CACHE BOOL "")
set(NEVERC_ENABLE_ORDER_FILE OFF CACHE BOOL "")
set(NEVERC_ENABLE_MIMALLOC ON CACHE BOOL "")
if(CMAKE_HOST_WIN32)
  set(NEVERC_STRIP_BINARY OFF CACHE BOOL "")
else()
  set(NEVERC_STRIP_BINARY ON CACHE BOOL "")
endif()

# LTO for the neverc binary.  Defaults OFF for fast incremental rebuilds
# during local development.  CI workflows pass -DNEVERC_ENABLE_LTO=ON so
# LTO is validated on every push.  Disabled under MSVC, when cross-compiling,
# or in Debug builds.
#
# On MSVC: /GL + /LTCG.  On Clang/GCC: LLVM ThinLTO by default (fast link,
# low memory, nearly identical runtime perf to Full LTO).  Override with
# -DNEVERC_LTO_MODE=Full for whole-program optimisation when build time and
# memory are not a concern.
option(NEVERC_ENABLE_LTO "Enable LTO for the neverc binary" OFF)
set(NEVERC_LTO_MODE "Thin" CACHE STRING "LTO flavour: Thin (default) or Full")
if(NEVERC_ENABLE_LTO AND NOT CMAKE_CROSSCOMPILING
   AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
  if(_NEVERC_HOST_MSVC)
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /GL" CACHE STRING "" FORCE)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /GL" CACHE STRING "" FORCE)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /LTCG" CACHE STRING "" FORCE)
    set(CMAKE_STATIC_LINKER_FLAGS "${CMAKE_STATIC_LINKER_FLAGS} /LTCG" CACHE STRING "" FORCE)
  else()
    set(LLVM_ENABLE_LTO "${NEVERC_LTO_MODE}" CACHE STRING "" FORCE)
  endif()
else()
  set(LLVM_ENABLE_LTO OFF CACHE STRING "" FORCE)
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
if(NOT _NEVERC_HOST_MSVC)
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
if(NOT _NEVERC_HOST_MSVC)
  set(CMAKE_C_FLAGS_RELEASE
      "${CMAKE_C_FLAGS_RELEASE} -ffunction-sections -fdata-sections"
      CACHE STRING "" FORCE)
  set(CMAKE_CXX_FLAGS_RELEASE
      "${CMAKE_CXX_FLAGS_RELEASE} -ffunction-sections -fdata-sections"
      CACHE STRING "" FORCE)
  if(NOT NEVERC_PGO_MODE STREQUAL "generate")
    if(APPLE)
      set(NEVERC_GC_LINKER_FLAG "-Wl,-dead_strip")
    elseif(WIN32)
      # lld-link performs dead-code elimination by default; --gc-sections
      # is an ELF linker flag that lld-link does not recognise.
      set(NEVERC_GC_LINKER_FLAG "")
    else()
      set(NEVERC_GC_LINKER_FLAG "-Wl,--gc-sections")
    endif()
    if(NEVERC_GC_LINKER_FLAG AND NOT CMAKE_EXE_LINKER_FLAGS MATCHES "${NEVERC_GC_LINKER_FLAG}")
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

if(NOT _NEVERC_HOST_MSVC)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-function" CACHE STRING "" FORCE)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-function" CACHE STRING "" FORCE)
endif()
