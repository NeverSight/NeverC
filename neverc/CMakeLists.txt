cmake_minimum_required(VERSION 3.20.0)

if(NOT DEFINED LLVM_COMMON_CMAKE_UTILS)
  set(LLVM_COMMON_CMAKE_UTILS ${CMAKE_CURRENT_SOURCE_DIR}/../cmake)
endif()
include(${LLVM_COMMON_CMAKE_UTILS}/Modules/CMakePolicy.cmake
  NO_POLICY_SCOPE)

# If we are not building as a part of LLVM, build NeverC as a
# standalone project, using LLVM as an external library:
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(NeverC)
  set(NEVERC_BUILT_STANDALONE TRUE)
endif()

# Must go below project(..)
include(GNUInstallDirs)

if(NEVERC_BUILT_STANDALONE)
  set(CMAKE_CXX_STANDARD 17 CACHE STRING "C++ standard to conform to")
  set(CMAKE_CXX_STANDARD_REQUIRED YES)
  set(CMAKE_CXX_EXTENSIONS NO)

  if(NOT MSVC_IDE)
    set(LLVM_ENABLE_ASSERTIONS ${ENABLE_ASSERTIONS}
      CACHE BOOL "Enable assertions")
    # Assertions should follow llvm-config's.
    mark_as_advanced(LLVM_ENABLE_ASSERTIONS)
  endif()

  find_package(LLVM REQUIRED HINTS "${LLVM_CMAKE_DIR}")
  list(APPEND CMAKE_MODULE_PATH "${LLVM_DIR}")

  # Turn into CACHE PATHs for overwritting
  set(LLVM_INCLUDE_DIRS ${LLVM_INCLUDE_DIRS} CACHE PATH "Path to llvm/include and any other header dirs needed")
  set(LLVM_BINARY_DIR "${LLVM_BINARY_DIR}" CACHE PATH "Path to LLVM build tree")
  set(LLVM_MAIN_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../llvm" CACHE PATH "Path to LLVM source tree")
  set(LLVM_TOOLS_BINARY_DIR "${LLVM_TOOLS_BINARY_DIR}" CACHE PATH "Path to llvm/bin")
  set(LLVM_LIBRARY_DIR "${LLVM_LIBRARY_DIR}" CACHE PATH "Path to llvm/lib")

  # They are used as destination of target generators.
  set(LLVM_RUNTIME_OUTPUT_INTDIR ${CMAKE_BINARY_DIR}/${CMAKE_CFG_INTDIR}/bin)
  set(LLVM_LIBRARY_OUTPUT_INTDIR ${CMAKE_BINARY_DIR}/${CMAKE_CFG_INTDIR}/lib${LLVM_LIBDIR_SUFFIX})
  if(WIN32 OR CYGWIN)
    # DLL platform -- put DLLs into bin.
    set(LLVM_SHLIB_OUTPUT_INTDIR ${LLVM_RUNTIME_OUTPUT_INTDIR})
  else()
    set(LLVM_SHLIB_OUTPUT_INTDIR ${LLVM_LIBRARY_OUTPUT_INTDIR})
  endif()

  option(LLVM_INSTALL_TOOLCHAIN_ONLY
    "Only include toolchain files in the 'install' target." ON)

  option(LLVM_FORCE_USE_OLD_TOOLCHAIN
    "Set to ON to force using an old, unsupported host toolchain." OFF)
  option(NEVERC_ENABLE_BOOTSTRAP "Generate the NeverC bootstrap target" OFF)

  include(AddLLVM)
  include(HandleLLVMOptions)
  include(VersionFromVCS)
  include(CheckAtomic)
  include(GetErrcMessages)
  include(LLVMDistributionSupport)

  set(PACKAGE_VERSION "${LLVM_PACKAGE_VERSION}")
  set(BUG_REPORT_URL "${LLVM_PACKAGE_BUGREPORT}" CACHE STRING
    "Default URL where bug reports are to be submitted.")

  if (NOT DEFINED LLVM_INCLUDE_TESTS)
    set(LLVM_INCLUDE_TESTS OFF)
  endif()

  include_directories(${LLVM_INCLUDE_DIRS})
  link_directories("${LLVM_LIBRARY_DIR}")

  set( CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin )
  set( CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib${LLVM_LIBDIR_SUFFIX} )
  set( CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib${LLVM_LIBDIR_SUFFIX} )

  if(LLVM_INCLUDE_TESTS)
    find_package(Python3 ${LLVM_MINIMUM_PYTHON_VERSION} REQUIRED
      COMPONENTS Interpreter)

    # Seek installed Lit (used by the check-neverc target).
    find_program(LLVM_LIT
                 NAMES llvm-lit lit.py lit
                 PATHS "${LLVM_MAIN_SRC_DIR}/utils/lit"
                 DOC "Path to lit.py")

    if(EXISTS ${LLVM_MAIN_SRC_DIR}/utils/lit/lit.py)
      if(EXISTS ${LLVM_MAIN_SRC_DIR}/utils/llvm-lit)
        add_subdirectory(${LLVM_MAIN_SRC_DIR}/utils/llvm-lit utils/llvm-lit)
      endif()
    endif()

    if (NOT TARGET llvm_gtest)
        message(FATAL_ERROR "llvm-gtest not found. Please install llvm-gtest or disable tests with -DLLVM_INCLUDE_TESTS=OFF")
    endif()

    if(LLVM_LIT)
      # Define the default arguments to use with 'lit', and an option for the user
      # to override.
      set(LIT_ARGS_DEFAULT "-sv")
      if (MSVC OR XCODE)
        set(LIT_ARGS_DEFAULT "${LIT_ARGS_DEFAULT} --no-progress-bar")
      endif()
      set(LLVM_LIT_ARGS "${LIT_ARGS_DEFAULT}" CACHE STRING "Default options for lit")

      get_errc_messages(LLVM_LIT_ERRC_MESSAGES)

      # On Win32 hosts, provide an option to specify the path to the GnuWin32 tools.
      if( WIN32 AND NOT CYGWIN )
        set(LLVM_LIT_TOOLS_DIR "" CACHE PATH "Path to GnuWin32 tools")
      endif()
    else()
      set(LLVM_INCLUDE_TESTS OFF)
    endif()

    umbrella_lit_testsuite_begin(check-all)
  endif() # LLVM_INCLUDE_TESTS
endif() # standalone

# Make sure that our source directory is on the current cmake module path so that
# we can include cmake files from this directory.
list(INSERT CMAKE_MODULE_PATH 0
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules"
  "${LLVM_COMMON_CMAKE_UTILS}/Modules"
  )

if(DEFINED HAVE_SYS_RESOURCE_H)
  set(NEVERC_HAVE_RLIMITS ${HAVE_SYS_RESOURCE_H})
else()
  include(CheckIncludeFile)
  check_include_file(sys/resource.h NEVERC_HAVE_RLIMITS)
endif()

set(NEVERC_RESOURCE_DIR "" CACHE STRING
  "Relative directory from the NeverC binary to its resource files.")

set(C_INCLUDE_DIRS "" CACHE STRING
  "Colon separated list of directories neverc will search for headers.")

set(GCC_INSTALL_PREFIX "" CACHE PATH "Directory where gcc is installed." )
set(DEFAULT_SYSROOT "" CACHE STRING
  "Default <path> to all compiler invocations for --sysroot=<path>." )

set(ENABLE_X86_RELAX_RELOCATIONS ON CACHE BOOL
    "enable x86 relax relocations by default")


set(NEVERC_DEFAULT_RTLIB "" CACHE STRING
  "Default runtime library to use (\"libgcc\" or \"compiler-rt\", empty for platform default)")
if (NOT(NEVERC_DEFAULT_RTLIB STREQUAL "" OR
        NEVERC_DEFAULT_RTLIB STREQUAL "libgcc" OR
        NEVERC_DEFAULT_RTLIB STREQUAL "compiler-rt"))
  message(WARNING "Resetting default rtlib to use platform default")
  set(NEVERC_DEFAULT_RTLIB "" CACHE STRING
    "Default runtime library to use (\"libgcc\" or \"compiler-rt\", empty for platform default)" FORCE)
endif()

set(NEVERC_DEFAULT_UNWINDLIB "" CACHE STRING
  "Default unwind library to use (\"none\" \"libgcc\" or \"libunwind\", empty to match runtime library.)")
if (NEVERC_DEFAULT_UNWINDLIB STREQUAL "")
  if (NEVERC_DEFAULT_RTLIB STREQUAL "libgcc")
    set (NEVERC_DEFAULT_UNWINDLIB "libgcc" CACHE STRING "" FORCE)
  endif()
endif()

if (NOT(NEVERC_DEFAULT_UNWINDLIB STREQUAL "" OR
        NEVERC_DEFAULT_UNWINDLIB STREQUAL "none" OR
        NEVERC_DEFAULT_UNWINDLIB STREQUAL "libgcc" OR
        NEVERC_DEFAULT_UNWINDLIB STREQUAL "libunwind"))
  message(WARNING "Resetting default unwindlib to use platform default")
  set(NEVERC_DEFAULT_UNWINDLIB "" CACHE STRING
    "Default unwind library to use (\"none\" \"libgcc\" or \"libunwind\", empty to match runtime library.)" FORCE)
endif()

set(NEVERC_DEFAULT_OBJECT_COPY_TOOL "objcopy" CACHE STRING
  "Default object copy executable to use.")

# Default macOS deployment target used when the user specifies no version
# (no -mmacosx-version-min, no MACOSX_DEPLOYMENT_TARGET, no versioned -target).
# Pinning a fixed value (instead of inheriting the build host's macOS version)
# makes default output upward-compatible: it runs on this version and every
# newer macOS.  Any explicit version still overrides it (even a lower one).
set(NEVERC_DARWIN_DEFAULT_DEPLOYMENT_TARGET "13.0" CACHE STRING
  "Default macOS deployment target when none is specified (e.g. \"13.0\").")

set(NEVERC_VENDOR ${PACKAGE_VENDOR} CACHE STRING
  "Vendor-specific text for showing with version information.")

set(NEVERC_REPOSITORY_STRING "" CACHE STRING
  "Vendor-specific text for showing the repository the source is taken from.")

if(NEVERC_REPOSITORY_STRING)
  add_definitions(-DNEVERC_REPOSITORY_STRING="${NEVERC_REPOSITORY_STRING}")
endif()


set(NEVERC_LINK_DYLIB OFF CACHE BOOL "" FORCE)

# The libdir suffix must exactly match whatever LLVM's configuration used.
set(NEVERC_LIBDIR_SUFFIX "${LLVM_LIBDIR_SUFFIX}")

set(NEVERC_TOOLS_INSTALL_DIR "${CMAKE_INSTALL_BINDIR}" CACHE PATH
    "Path for binary subdirectory (defaults to '${CMAKE_INSTALL_BINDIR}')")
mark_as_advanced(NEVERC_TOOLS_INSTALL_DIR)

set(NEVERC_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(NEVERC_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR})

if( CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR AND NOT MSVC_IDE )
  message(FATAL_ERROR "In-source builds are not allowed. "
"Please create a directory and run cmake "
"from there, passing the path to this source directory as the last argument. "
"This process created the file `CMakeCache.txt' and the directory "
"`CMakeFiles'. Please delete them.")
endif()

# If NEVERC_VERSION_* is specified, use it, if not use LLVM_VERSION_*.
if(NOT DEFINED NEVERC_VERSION_MAJOR)
  set(NEVERC_VERSION_MAJOR ${LLVM_VERSION_MAJOR})
endif()
if(NOT DEFINED NEVERC_VERSION_MINOR)
  set(NEVERC_VERSION_MINOR ${LLVM_VERSION_MINOR})
endif()
if(NOT DEFINED NEVERC_VERSION_PATCHLEVEL)
  set(NEVERC_VERSION_PATCHLEVEL ${LLVM_VERSION_PATCH})
endif()
set(NEVERC_VERSION "${NEVERC_VERSION_MAJOR}.${NEVERC_VERSION_MINOR}.${NEVERC_VERSION_PATCHLEVEL}")
message(STATUS "NeverC version: ${NEVERC_VERSION}")

# Configure the Version.inc file.
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/include/neverc/Foundation/Core/Version.inc.in
  ${CMAKE_CURRENT_BINARY_DIR}/include/neverc/Foundation/Core/Version.inc)

# Add appropriate flags for GCC
if (LLVM_COMPILER_IS_GCC_COMPATIBLE)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-common -Woverloaded-virtual")
  if (NOT "${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-strict-aliasing")
  endif ()

  # Enable -pedantic even if it's not enabled for LLVM.
  if (NOT LLVM_ENABLE_PEDANTIC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pedantic -Wno-long-long")
  endif ()

  if("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-nested-anon-types" )
  endif()
endif ()

set(HOST_LINK_VERSION "" CACHE STRING
  "Host linker version string to report in neverc --version.")
option(NEVERC_DETECT_HOST_LINK_VERSION
  "Detect the Darwin host linker version during configure." OFF)
if (NEVERC_DETECT_HOST_LINK_VERSION AND APPLE AND NOT CMAKE_LINKER MATCHES ".*lld.*")
  set(LD_V_OUTPUT)
  execute_process(
    COMMAND sh -c "${CMAKE_LINKER} -v 2>&1 | head -1"
    RESULT_VARIABLE HAD_ERROR
    OUTPUT_VARIABLE LD_V_OUTPUT
  )
  if (HAD_ERROR)
    message(FATAL_ERROR "${CMAKE_LINKER} failed with status ${HAD_ERROR}")
  endif()
  if ("${LD_V_OUTPUT}" MATCHES ".*ld64-([0-9.]+).*")
    string(REGEX REPLACE ".*ld64-([0-9.]+).*" "\\1" HOST_LINK_VERSION ${LD_V_OUTPUT})
  elseif ("${LD_V_OUTPUT}" MATCHES "[^0-9]*([0-9.]+).*")
    string(REGEX REPLACE "[^0-9]*([0-9.]+).*" "\\1" HOST_LINK_VERSION ${LD_V_OUTPUT})
  endif()
  message(STATUS "Host linker version: ${HOST_LINK_VERSION}")
endif()

include(CMakeParseArguments)
include(AddNeverC)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

include_directories(BEFORE
  ${CMAKE_CURRENT_BINARY_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  )

if (NOT LLVM_INSTALL_TOOLCHAIN_ONLY)
  install(DIRECTORY include/neverc
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    COMPONENT neverc-headers
    FILES_MATCHING
    PATTERN "*.def"
    PATTERN "*.h"
    PATTERN "config.h" EXCLUDE
    )

  install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/include/neverc
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    COMPONENT neverc-headers
    FILES_MATCHING
    PATTERN "CMakeFiles" EXCLUDE
    PATTERN "*.inc"
    PATTERN "*.h"
    )

  add_custom_target(neverc-headers)
  set_target_properties(neverc-headers PROPERTIES FOLDER "Misc")
  if(NOT LLVM_ENABLE_IDE)
    add_llvm_install_targets(install-neverc-headers
                             DEPENDS neverc-headers
                             COMPONENT neverc-headers)
  endif()

endif()

option(NEVERC_BUILD_TOOLS
  "Build the NeverC tools. If OFF, just generate build targets." ON)

if(LLVM_ENABLE_PLUGINS OR LLVM_EXPORT_SYMBOLS_FOR_PLUGINS)
  set(HAVE_NEVERC_PLUGIN_SUPPORT ON)
else()
  set(HAVE_NEVERC_PLUGIN_SUPPORT OFF)
endif()
CMAKE_DEPENDENT_OPTION(NEVERC_PLUGIN_SUPPORT
  "Build NeverC with plugin support" ON
  "HAVE_NEVERC_PLUGIN_SUPPORT" OFF)

option(NEVERC_ENABLE_PYTHON_PLUGINS
  "Enable embedded CPython bindings for -fplugin=*.py" OFF)
option(NEVERC_BUNDLE_PYTHON_RUNTIME
  "Install NeverC with a relocatable adjacent CPython runtime" OFF)

if(NEVERC_BUNDLE_PYTHON_RUNTIME AND NOT NEVERC_ENABLE_PYTHON_PLUGINS)
  message(FATAL_ERROR
    "NEVERC_BUNDLE_PYTHON_RUNTIME=ON requires "
    "NEVERC_ENABLE_PYTHON_PLUGINS=ON")
endif()

if(NEVERC_ENABLE_BOOTSTRAP)
  message(FATAL_ERROR
    "NeverC: NEVERC_ENABLE_BOOTSTRAP is unsupported (NeverC is a C-only compiler).")
endif()

# NeverC version information
set(NEVERC_EXECUTABLE_VERSION
    "${NEVERC_VERSION_MAJOR}" CACHE STRING
    "Major version number that will be appended to the NeverC executable name")
mark_as_advanced(NEVERC_EXECUTABLE_VERSION)

add_subdirectory(include)

add_custom_target(neverc-tablegen-targets)
set_target_properties(neverc-tablegen-targets PROPERTIES FOLDER "Misc")

# Force target to be built as soon as possible. Module builds depend
# header-wise on it as they ship all headers from the umbrella folders. Building
# an entire module might include header, which depends on intrinsics_gen.
if(LLVM_ENABLE_MODULES)
  list(APPEND LLVM_COMMON_DEPENDS intrinsics_gen)
endif()

# mimalloc high-performance allocator
#
# Fetch it before adding lib/Foundation: that directory defines the bootstrap
# targets which compile mimalloc into the embedded per-OS bitcode blobs.
option(NEVERC_ENABLE_MIMALLOC
  "Replace the system allocator with mimalloc (static)." ON)
# mimalloc's MI_OVERRIDE=ON replaces malloc/free/new/delete process-wide,
# which conflicts with any sanitizer that installs its own allocator
# interceptors: ASan flags every libc++-side free as a bad-free, while
# TSan/MSan lose the shadow-memory bookkeeping they attach to each
# allocation and either crash or report spuriously. Auto-disable mimalloc
# whenever such a sanitizer build is detected.
if(NEVERC_ENABLE_MIMALLOC AND
   (LLVM_USE_SANITIZER MATCHES "Address|Thread|Memory" OR
    "${CMAKE_CXX_FLAGS}" MATCHES "fsanitize=(address|thread|memory)" OR
    "${CMAKE_C_FLAGS}" MATCHES "fsanitize=(address|thread|memory)"))
  message(STATUS
    "NeverC: allocator-intercepting sanitizer detected; disabling mimalloc "
    "to avoid MI_OVERRIDE conflicts with the sanitizer's malloc interceptor.")
  set(NEVERC_ENABLE_MIMALLOC OFF)
endif()
if(NEVERC_ENABLE_MIMALLOC)
  include(AddMimalloc)
  neverc_fetch_mimalloc()
endif()

add_subdirectory(lib)
# The linker lives under lib/Linker/ as a NeverC subproject; its static
# libraries are consumed by the `neverc` executable below.
add_subdirectory(lib/Linker)

# NeverC standard library (std)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../std
                 ${CMAKE_CURRENT_BINARY_DIR}/std)

# Platform-specific runtime SDKs (WDK, future Windows SDK, etc.)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../runtime
                 ${CMAKE_CURRENT_BINARY_DIR}/runtime)

option(NEVERC_ENABLE_BENCH "Build NeverC microbenchmarks" OFF)
if(NEVERC_ENABLE_BENCH)
  add_subdirectory(bench)
endif()

# ---------------------------------------------------------------------------
# neverc executable — single binary serving as both compiler and linker.
# ---------------------------------------------------------------------------
if(NEVERC_PLUGIN_SUPPORT)
  set(support_plugins SUPPORT_PLUGINS)
endif()

add_neverc_tool(neverc
  Driver/ArgumentHandlers.cpp
  main.cpp

  DEPENDS
  intrinsics_gen
  ${support_plugins}
  GENERATE_DRIVER
  )

if(NEVERC_BUNDLE_PYTHON_RUNTIME AND NOT WIN32)
  if(APPLE)
    set(_neverc_python_install_rpath
      "@executable_path/../python/lib")
  else()
    set(_neverc_python_install_rpath
      "\$ORIGIN/../python/lib")
  endif()
  # Keep the build-tree Python search path computed by CMake, but replace it
  # with the adjacent runtime path in installed/package artifacts.
  set_target_properties(neverc PROPERTIES
    BUILD_WITH_INSTALL_RPATH OFF
    INSTALL_RPATH "${_neverc_python_install_rpath}"
    )
endif()

neverc_target_link_libraries(neverc
  PRIVATE
  nevercBuild
  nevercRun
  nevercRuntime
  nevercUpdate
  nevercEmit
  nevercDynCode
  nevercPluginLink
  nevercStrHash
  nevercXorStr
  )

# Embed the enabled linker backends so flavor dispatch can call each
# backend's link() directly.
if(NOT LINKER_ENABLED_BACKEND_LIBS)
  message(FATAL_ERROR "NeverC requires at least one enabled linker backend")
endif()
target_link_libraries(neverc PRIVATE ${LINKER_ENABLED_BACKEND_LIBS})
foreach(_linker_backend IN LISTS LINKER_ENABLED_BACKEND_LIBS)
  if(_linker_backend STREQUAL "linkerCOFF")
    target_compile_definitions(neverc PRIVATE LINKER_ENABLE_DRIVER_COFF=1)
  elseif(_linker_backend STREQUAL "linkerELF")
    target_compile_definitions(neverc PRIVATE LINKER_ENABLE_DRIVER_ELF=1)
  elseif(_linker_backend STREQUAL "linkerMachO")
    target_compile_definitions(neverc PRIVATE LINKER_ENABLE_DRIVER_MACHO=1)
  endif()
endforeach()
target_include_directories(neverc PRIVATE
  "${NEVERC_SOURCE_DIR}/include/neverc"
  "${NEVERC_BINARY_DIR}/lib/Linker/include"
  )

if(NEVERC_ENABLE_MIMALLOC)
  neverc_link_mimalloc(neverc)
endif()

# Windows CI links with -nostdlib (minimal CRT via --dependent-lib=msvcrt), so
# Clang does not pull in compiler-rt automatically. Full LTO then needs explicit
# builtins for helpers such as __udivti3 (e.g. from llvm::APInt::toString).
if(WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  include(HandleCompilerRT)
  find_compiler_rt_library(builtins NEVERC_COMPILER_RT_BUILTINS
    TARGET "${LLVM_HOST_TRIPLE}")
  if(NEVERC_COMPILER_RT_BUILTINS AND NOT NEVERC_COMPILER_RT_BUILTINS STREQUAL "NOTFOUND")
    message(STATUS "NeverC: linking compiler-rt builtins: ${NEVERC_COMPILER_RT_BUILTINS}")
    target_link_libraries(neverc PRIVATE "${NEVERC_COMPILER_RT_BUILTINS}")
  elseif(LLVM_ENABLE_LTO)
    message(FATAL_ERROR
      "NeverC: could not find compiler-rt builtins for ${LLVM_HOST_TRIPLE}. "
      "Required on Windows with LTO when the link uses -nostdlib "
      "(undefined symbols such as __udivti3).")
  else()
    set(NEVERC_COMPILER_RT_BUILTINS "")
  endif()
endif()

# Windows main-thread stack reserve.
#
# The whole-program LTO optimizer recurses deeply on adversarial auto-LTO input:
# a caps-off "collapsed" giant function (inline/unroll caps disabled) drives
# ScalarEvolution::getSCEVAtScope into a deep self-recursion over the nested SCEV
# expression.  That serial IPO/optimization phase runs on neverc's MAIN thread,
# whose default reserved stack on Windows is only 1 MiB -- versus 8 MiB on Linux
# (pthread/main) and macOS, where the *identical* IR optimizes fine.  That lone
# platform gap is why LTOTest.AutoLtoLoopDenseNoCompileCliff / .Auto-
# LtoInlineCapSemanticsPreserved overflowed the stack (STATUS_STACK_OVERFLOW
# 0xC00000FD, surfacing as the 0xC0000409 fast-fail exit code) only on Windows.
# The recursion is bounded (it completes within 8 MiB elsewhere), so reserving a
# generous 64 MiB main-thread stack -- address space only; pages commit on demand
# -- gives Windows ample headroom.  This is the main-thread counterpart to the
# 64 MiB llvm::thread codegen-worker stack (llvm/include/llvm/Support/thread.h):
# the worker pool covers per-partition opt, this covers the serial whole-program
# phase.  Set on the target (not CMAKE_EXE_LINKER_FLAGS) so a CI flag override
# cannot drop it; /STACK is understood by both lld-link and MSVC link.
if(WIN32)
  target_link_options(neverc PRIVATE "LINKER:/STACK:0x4000000")
endif()

option(NEVERC_STRIP_BINARY "Strip local symbols from the neverc binary after linking" OFF)
if(NEVERC_STRIP_BINARY AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
  if(WIN32)
    # On Windows, only use llvm-strip which handles all PE architectures.
    # MinGW's strip.exe is often x86_64-only and chokes on ARM64 PE files.
    find_program(NEVERC_STRIP_PROG NAMES llvm-strip)
  else()
    find_program(NEVERC_STRIP_PROG NAMES llvm-strip strip)
  endif()
  if(NEVERC_STRIP_PROG)
    if(APPLE)
      set(_strip_flags -x)
    elseif(UNIX)
      set(_strip_flags --strip-unneeded)
    else()
      set(_strip_flags "")
    endif()
    add_custom_command(TARGET neverc POST_BUILD
      COMMAND "${NEVERC_STRIP_PROG}" ${_strip_flags} "$<TARGET_FILE:neverc>"
      COMMENT "Stripping neverc binary")
  endif()
endif()

set(NEVERC_LINKS_TO_CREATE "" CACHE STRING "" FORCE)

if(NEVERC_PLUGIN_SUPPORT)
  export_executable_symbols_for_plugins(neverc)
endif()

add_dependencies(neverc neverc-resource-headers)
if(TARGET neverc-runtime-android-kernel)
  # Stage the kernel SDK next to the binary so -fandroid-kernel-driver-mode
  # finds headers/libs relative to InstalledDir without a full SDK copy.
  add_dependencies(neverc neverc-runtime-android-kernel)
endif()

# Mimalloc per-OS bootstrap cross-compiles need the matching bundled
# sysroot headers (e.g. <wchar.h>).  runtime/ is add_subdirectory'd after
# lib/, so these edges cannot be declared in Foundation/CMakeLists.txt —
# wire them here once both sides of the graph exist.
#
# A targeted `--target neverc-embed-runtime-bitcode` then pulls only the
# sysroots each bootstrap actually needs, not the full SDK set.
if(TARGET neverc-bootstrap-mimalloc-bc-linux_x64 AND TARGET neverc-runtime-linux-x64)
  add_dependencies(neverc-bootstrap-mimalloc-bc-linux_x64 neverc-runtime-linux-x64)
endif()
if(TARGET neverc-bootstrap-mimalloc-bc-linux_arm64 AND TARGET neverc-runtime-linux-arm64)
  add_dependencies(neverc-bootstrap-mimalloc-bc-linux_arm64 neverc-runtime-linux-arm64)
endif()
if(TARGET neverc-bootstrap-mimalloc-bc-darwin_x64 AND TARGET neverc-runtime-macos-arm64)
  add_dependencies(neverc-bootstrap-mimalloc-bc-darwin_x64 neverc-runtime-macos-arm64)
endif()
if(TARGET neverc-bootstrap-mimalloc-bc-darwin_arm64 AND TARGET neverc-runtime-macos-arm64)
  add_dependencies(neverc-bootstrap-mimalloc-bc-darwin_arm64 neverc-runtime-macos-arm64)
endif()
if(TARGET neverc-bootstrap-mimalloc-bc-win_x64)
  add_dependencies(neverc-bootstrap-mimalloc-bc-win_x64 neverc-runtime-shared-msvc)
endif()
if(TARGET neverc-bootstrap-mimalloc-bc-win_arm64)
  add_dependencies(neverc-bootstrap-mimalloc-bc-win_arm64 neverc-runtime-shared-msvc)
endif()

# Each std bootstrap target cross-compiles for its own --target and therefore
# reads that target's staged SDK.  Put the dependency on each child target (not
# only on the aggregate target), otherwise Ninja may compile a child while an
# ALL runtime target is removing and recopying the same SDK tree.
foreach(_std_runtime_spec IN ITEMS
    "linux_x64:neverc-runtime-linux-x64"
    "linux_arm64:neverc-runtime-linux-arm64"
    "darwin_x64:neverc-runtime-macos-arm64"
    "darwin_arm64:neverc-runtime-macos-arm64"
    "win_x64:neverc-runtime-shared-msvc"
    "win_arm64:neverc-runtime-shared-msvc")
  string(REPLACE ":" ";" _std_runtime_parts "${_std_runtime_spec}")
  list(GET _std_runtime_parts 0 _std_runtime_tag)
  list(GET _std_runtime_parts 1 _std_runtime_dep)
  if(TARGET neverc-bootstrap-std-bc-${_std_runtime_tag} AND
     TARGET ${_std_runtime_dep})
    add_dependencies(neverc-bootstrap-std-bc-${_std_runtime_tag}
                     ${_std_runtime_dep})
  endif()
endforeach()
unset(_std_runtime_spec)
unset(_std_runtime_parts)
unset(_std_runtime_tag)
unset(_std_runtime_dep)

# Stage-2 neverc: embed bootstrapped runtime bitcode into the compiler.
#
# Stage 1 links neverc with empty bitcode placeholders so the compiler can
# compile. Bootstrap targets then use that binary to produce real bitcode
# headers and touch Builtin*.cpp; the rebuild below picks those up. Prefer
# `neverc-embed-runtime-bitcode` (or `check-neverc`, which depends on it)
# over a bare `neverc` whenever embedded string / mimalloc / std / NVK
# runtimes must be present.
if(NEVERC_BUILT_STANDALONE)
  set(_NEVERC_TOP_BUILD_DIR "${CMAKE_BINARY_DIR}")
else()
  set(_NEVERC_TOP_BUILD_DIR "${LLVM_BINARY_DIR}")
endif()

set(_NEVERC_RUNTIME_BOOTSTRAP_TARGETS
  neverc-bootstrap-string-bc
  neverc-bootstrap-std-bc
  neverc-bootstrap-nvk-kernel-bc)
if(TARGET neverc-bootstrap-mimalloc-bc)
  list(APPEND _NEVERC_RUNTIME_BOOTSTRAP_TARGETS neverc-bootstrap-mimalloc-bc)
endif()

add_custom_target(neverc-embed-runtime-bitcode
  COMMAND ${CMAKE_COMMAND} --build "${_NEVERC_TOP_BUILD_DIR}"
          --config $<CONFIG> --parallel --target neverc
  DEPENDS ${_NEVERC_RUNTIME_BOOTSTRAP_TARGETS}
  COMMENT "[NeverC] Relinking neverc with embedded runtime bitcode"
  USES_TERMINAL
)

if(NEVERC_ORDER_FILE AND
    (LLVM_LINKER_IS_LD64 OR LLVM_LINKER_IS_GOLD OR LLVM_LINKER_IS_LLD))
  include(LLVMCheckLinkerFlag)

  if(LLVM_LINKER_IS_LD64 OR (LLVM_LINKER_IS_LLD AND APPLE))
    set(LINKER_ORDER_FILE_OPTION "-Wl,-order_file,${NEVERC_ORDER_FILE}")
  elseif(LLVM_LINKER_IS_GOLD)
    set(LINKER_ORDER_FILE_OPTION "-Wl,--section-ordering-file,${NEVERC_ORDER_FILE}")
  elseif(LLVM_LINKER_IS_LLD)
    set(LINKER_ORDER_FILE_OPTION "-Wl,--symbol-ordering-file,${NEVERC_ORDER_FILE}")
  endif()

  llvm_check_linker_flag(CXX ${LINKER_ORDER_FILE_OPTION} LINKER_ORDER_FILE_WORKS)

  file(READ ${NEVERC_ORDER_FILE} ORDER_FILE LIMIT 20)
  if("${ORDER_FILE}" STREQUAL "\n")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${NEVERC_ORDER_FILE})
  elseif(LINKER_ORDER_FILE_WORKS)
    target_link_libraries(neverc PRIVATE ${LINKER_ORDER_FILE_OPTION})
    set_target_properties(neverc PROPERTIES LINK_DEPENDS ${NEVERC_ORDER_FILE})
  endif()
endif()

# Out-of-tree plugin SDK (header + examples + build template). Keep this after
# the compiler target so SDK examples can also be built with the new NeverC.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../pluginsdk
                 ${CMAKE_CURRENT_BINARY_DIR}/pluginsdk)

option(NEVERC_ENABLE_ORDER_FILE
  "Generate and validate a Darwin order file for neverc." OFF)
if(APPLE AND NEVERC_ENABLE_ORDER_FILE)
  set(NEVERC_ORDER_FILE ${CMAKE_CURRENT_BINARY_DIR}/neverc.order CACHE FILEPATH
    "Order file to use when compiling neverc in order to improve startup time (Darwin Only - requires ld64).")

  if(NOT EXISTS ${NEVERC_ORDER_FILE})
    string(FIND "${NEVERC_ORDER_FILE}" "${CMAKE_CURRENT_BINARY_DIR}" PATH_START)
    if(PATH_START EQUAL 0)
      file(WRITE ${NEVERC_ORDER_FILE} "\n")
    else()
      message(FATAL_ERROR "Specified order file '${NEVERC_ORDER_FILE}' does not exist.")
    endif()
  endif()
else()
  unset(NEVERC_ORDER_FILE CACHE)
  unset(NEVERC_ORDER_FILE)
endif()


if(NOT LLVM_INSTALL_TOOLCHAIN_ONLY)
  # Custom target to install all NeverC libraries.
  add_custom_target(neverc-libraries)
  set_target_properties(neverc-libraries PROPERTIES FOLDER "Misc")

  if(NOT LLVM_ENABLE_IDE)
    add_llvm_install_targets(install-neverc-libraries
                             DEPENDS neverc-libraries
                             COMPONENT neverc-libraries)
  endif()

  get_property(NEVERC_LIBS GLOBAL PROPERTY NEVERC_LIBS)
  if(NEVERC_LIBS)
    list(REMOVE_DUPLICATES NEVERC_LIBS)
    foreach(lib ${NEVERC_LIBS})
      add_dependencies(neverc-libraries ${lib})
      if(NOT LLVM_ENABLE_IDE)
        add_dependencies(install-neverc-libraries install-${lib})
        add_dependencies(install-neverc-libraries-stripped install-${lib}-stripped)
      endif()
    endforeach()
  endif()
endif()

if(NOT LLVM_INSTALL_TOOLCHAIN_ONLY)
  add_subdirectory(cmake/modules)
endif()

option(NEVERC_BOLT_INSTRUMENT "Generate NeverC BOLT instrumentation targets" OFF)
if(NEVERC_BOLT_INSTRUMENT)
  message(FATAL_ERROR
    "NEVERC_BOLT_INSTRUMENT is unsupported in NeverC; llvm-bolt is not part of this tree.")
endif()

if(NEVERC_BUILT_STANDALONE)
  if(NOT LLVM_INSTALL_TOOLCHAIN_ONLY)
    llvm_distribution_add_targets()
  endif()
  if(LLVM_ENABLE_PLUGINS OR LLVM_EXPORT_SYMBOLS_FOR_PLUGINS)
    process_llvm_pass_plugins()
  endif()
endif()

option(NEVERC_INCLUDE_TESTS "Build NeverC GoogleTest-based test suite" OFF)
set(NEVERC_TEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../tests")
if(NEVERC_INCLUDE_TESTS AND EXISTS "${NEVERC_TEST_DIR}/CMakeLists.txt")
  add_subdirectory("${NEVERC_TEST_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/tests")

  # The suite compiles & links real programs with the freshly-built neverc,
  # which finds its system headers/libraries from the bundled SDK staged under
  # <build>/runtime. Those copy targets are ALL-targets, so a plain
  # `--target check-neverc` would otherwise skip them and every compile/link
  # test would fail (no <stdio.h>, no CRT). Depend on them explicitly so the
  # suite is self-contained.  (neverc-embed-runtime-bitcode only pulls the
  # narrower sysroots needed for mimalloc bootstrap — not this full set.)
  #
  # Shared MSVC headers are needed on every host: WinArm64IntrinTest does
  # cross-compiles to aarch64-pc-windows-msvc and #includes <windows.h>.
  set(_neverc_check_runtime_deps neverc-runtime-shared-msvc)
  if(CMAKE_HOST_WIN32)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64")
      list(APPEND _neverc_check_runtime_deps neverc-runtime-arm64-msvc)
    else()
      list(APPEND _neverc_check_runtime_deps neverc-runtime-x64-msvc)
    endif()
  endif()
  # Cross-compilation tests (CrossAArch64, CrossAndroid) need the bundled
  # Linux/Android sysroots copied into the build tree.
  if(TARGET neverc-runtime-linux-x64)
    list(APPEND _neverc_check_runtime_deps neverc-runtime-linux-x64)
  endif()
  if(TARGET neverc-runtime-linux-arm64)
    list(APPEND _neverc_check_runtime_deps neverc-runtime-linux-arm64)
  endif()
  if(TARGET neverc-runtime-android-arm64)
    list(APPEND _neverc_check_runtime_deps neverc-runtime-android-arm64)
  endif()
  if(TARGET neverc-runtime-android-kernel)
    list(APPEND _neverc_check_runtime_deps neverc-runtime-android-kernel)
  endif()
  # On macOS the *native* compile/link tests need the bundled macOS sysroot
  # staged too: getBundledMacOSSysroot() looks for <build>/runtime/macos/<arch>
  # and only falls back to the host Xcode SDK when it is missing.  That fallback
  # used to work, but the macos-latest runner now ships macOS 26 (SDK reports
  # itself as macOS 16.0), whose libSystem.tbd the embedded MachO linker rejects
  # as "incompatible with arm64 (macOS16.0.0)".  Staging the hermetic bundled
  # sysroot keeps the suite self-contained and independent of the host SDK.
  if(TARGET neverc-runtime-macos-arm64)
    list(APPEND _neverc_check_runtime_deps neverc-runtime-macos-arm64)
  endif()

  # CTest registers every discovered GoogleTest executable in the test tree.
  # Build those executables when invoking check-neverc directly; relying on a
  # prior `all` build leaves `_NOT_BUILT` placeholders that fail the gate.
  set(_neverc_check_test_deps
    neverc-tests
    neverc-build-tests
    neverc-update-tests
    neverc-run-tests
    neverc-merge-tests
    neverc-std-tests
  )
  foreach(_neverc_test_target IN ITEMS
      neverc-plugin-core-tests
      neverc-plugin-frontend-tests
      neverc-plugin-ir-tests
      neverc-plugin-target-mc-tests
      neverc-plugin-dyncode-tests
      neverc-plugin-link-tests
      neverc-plugin-object-merge-tests
      neverc-plugin-lto-input-tests
      neverc-support-tests
      neverc-plugin-io-tests
      neverc-plugin-conformance)
    if(TARGET ${_neverc_test_target})
      list(APPEND _neverc_check_test_deps ${_neverc_test_target})
    endif()
  endforeach()
  add_custom_target(check-neverc
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -C $<CONFIG>
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tests
    DEPENDS neverc-embed-runtime-bitcode
            ${_neverc_check_test_deps}
            ${_neverc_check_runtime_deps}
    USES_TERMINAL
    COMMENT "Running NeverC test suite (GoogleTest + CTest)")

  add_custom_target(check-neverc-std
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -C $<CONFIG> -R "StdLibTest[.]"
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tests
    DEPENDS neverc-embed-runtime-bitcode neverc-std-tests
            ${_neverc_check_runtime_deps}
    USES_TERMINAL
    COMMENT "Running NeverC stdlib tests")
endif()

option(NEVERC_ENABLE_PLUGIN_FUZZERS
  "Build the Clang/libFuzzer plugin ABI targets" OFF)
if(NEVERC_ENABLE_PLUGIN_FUZZERS)
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../fuzz"
                   "${CMAKE_CURRENT_BINARY_DIR}/fuzz")
endif()

set(NEVERC_INSTALL_LIBDIR_BASENAME "lib${NEVERC_LIBDIR_SUFFIX}")

configure_file(
  ${NEVERC_SOURCE_DIR}/include/neverc/Config/config.h.cmake
  ${NEVERC_BINARY_DIR}/include/neverc/Config/config.h)
