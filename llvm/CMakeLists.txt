# See docs/CMake.html for instructions about how to build LLVM with CMake.

cmake_minimum_required(VERSION 3.20.0)

set(LLVM_COMMON_CMAKE_UTILS ${CMAKE_CURRENT_SOURCE_DIR}/../cmake)
include(${LLVM_COMMON_CMAKE_UTILS}/Modules/CMakePolicy.cmake
  NO_POLICY_SCOPE)

# Enable /MP on MSVC
if(MSVC)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /MP")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")
endif()

# Builds with custom install names and installation rpath setups may not work
# in the build tree. Allow these cases to use CMake's default build tree
# behavior by setting `LLVM_NO_INSTALL_NAME_DIR_FOR_BUILD_TREE` to do this.
option(LLVM_NO_INSTALL_NAME_DIR_FOR_BUILD_TREE "If set, use CMake's default build tree install name directory logic (Darwin only)" OFF)
mark_as_advanced(LLVM_NO_INSTALL_NAME_DIR_FOR_BUILD_TREE)
if(NOT LLVM_NO_INSTALL_NAME_DIR_FOR_BUILD_TREE)
  set(CMAKE_BUILD_WITH_INSTALL_NAME_DIR ON)
endif()

if(NOT DEFINED LLVM_VERSION_MAJOR)
  set(LLVM_VERSION_MAJOR 3389)
endif()
if(NOT DEFINED LLVM_VERSION_MINOR)
  set(LLVM_VERSION_MINOR 1)
endif()
if(NOT DEFINED LLVM_VERSION_PATCH)
  set(LLVM_VERSION_PATCH 0)
endif()
if(NOT DEFINED LLVM_VERSION_SUFFIX)
  set(LLVM_VERSION_SUFFIX newworld)
endif()

if (NOT PACKAGE_VERSION)
  set(PACKAGE_VERSION
    "${LLVM_VERSION_MAJOR}.${LLVM_VERSION_MINOR}.${LLVM_VERSION_PATCH}${LLVM_VERSION_SUFFIX}")
endif()

if(NOT DEFINED LLVM_SHLIB_SYMBOL_VERSION)
  # "Symbol version prefix for libLLVM.so"
  set(LLVM_SHLIB_SYMBOL_VERSION "LLVM_${LLVM_VERSION_MAJOR}")
endif()

if ((CMAKE_GENERATOR MATCHES "Visual Studio") AND (MSVC_TOOLSET_VERSION LESS 142) AND (CMAKE_GENERATOR_TOOLSET STREQUAL ""))
  message(WARNING "Visual Studio generators use the x86 host compiler by "
                  "default, even for 64-bit targets. This can result in linker "
                  "instability and out of memory errors. To use the 64-bit "
                  "host compiler, pass -Thost=x64 on the CMake command line.")
endif()

if (CMAKE_GENERATOR STREQUAL "Xcode" AND NOT CMAKE_OSX_ARCHITECTURES)
  # Some CMake features like object libraries get confused if you don't
  # explicitly specify an architecture setting with the Xcode generator.
  set(CMAKE_OSX_ARCHITECTURES "x86_64")
endif()

project(LLVM
  VERSION ${LLVM_VERSION_MAJOR}.${LLVM_VERSION_MINOR}.${LLVM_VERSION_PATCH}
  LANGUAGES C CXX ASM)

if (NOT DEFINED CMAKE_INSTALL_LIBDIR AND DEFINED LLVM_LIBDIR_SUFFIX)
  # Must go before `include(GNUInstallDirs)`.
  set(CMAKE_INSTALL_LIBDIR "lib${LLVM_LIBDIR_SUFFIX}")
endif()

# Must go after `DEFINED LLVM_LIBDIR_SUFFIX` check.
set(LLVM_LIBDIR_SUFFIX "" CACHE STRING "Define suffix of library directory name (32/64)" )

# Must go after `project(..)`.
include(GNUInstallDirs)

# This C++ standard is required to build LLVM.
set(LLVM_REQUIRED_CXX_STANDARD 17)

# If we find that the cache contains CMAKE_CXX_STANDARD it means that it's a old CMakeCache.txt
# and we can just inform the user and then reset it.
if($CACHE{CMAKE_CXX_STANDARD} AND $CACHE{CMAKE_CXX_STANDARD} LESS ${LLVM_REQUIRED_CXX_STANDARD})
  message(WARNING "Resetting cache value for CMAKE_CXX_STANDARD to ${LLVM_REQUIRED_CXX_STANDARD}")
  unset(CMAKE_CXX_STANDARD CACHE)
endif()

# if CMAKE_CXX_STANDARD is still set after the cache unset above it means that the user requested it
# and we allow it to be set to something newer than the required standard but otherwise we fail.
if(DEFINED CMAKE_CXX_STANDARD AND CMAKE_CXX_STANDARD LESS ${LLVM_REQUIRED_CXX_STANDARD})
  message(FATAL_ERROR "Requested CMAKE_CXX_STANDARD=${CMAKE_CXX_STANDARD} which is less than the required ${LLVM_REQUIRED_CXX_STANDARD}.")
endif()

set(CMAKE_CXX_STANDARD ${LLVM_REQUIRED_CXX_STANDARD} CACHE STRING "C++ standard to conform to")

set(CMAKE_CXX_STANDARD_REQUIRED YES)

if (CYGWIN)
  # Cygwin is a bit stricter and lack things like 'strdup', 'stricmp', etc in
  # c++xx mode.
  set(CMAKE_CXX_EXTENSIONS YES)
else()
  set(CMAKE_CXX_EXTENSIONS NO)
endif()

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(FATAL_ERROR "
No build type selected. You need to pass -DCMAKE_BUILD_TYPE=<type> in order to configure LLVM.
Available options are:
  * -DCMAKE_BUILD_TYPE=Release - For an optimized build with no assertions or debug info.
  * -DCMAKE_BUILD_TYPE=Debug - For an unoptimized build with assertions and debug info.
  * -DCMAKE_BUILD_TYPE=RelWithDebInfo - For an optimized build with no assertions but with debug info.
  * -DCMAKE_BUILD_TYPE=MinSizeRel - For a build optimized for size instead of speed.
Learn more about these options in our documentation at https://llvm.org/docs/CMake.html#cmake-build-type
")
endif()

# Set default build type for cmake's try_compile module.
# CMake 3.17 or newer sets CMAKE_DEFAULT_BUILD_TYPE to one of the
# items from CMAKE_CONFIGURATION_TYPES. Logic below can be further
# simplified once LLVM's minimum CMake version is updated to 3.17.
if(CMAKE_DEFAULT_BUILD_TYPE)
  set(CMAKE_TRY_COMPILE_CONFIGURATION ${CMAKE_DEFAULT_BUILD_TYPE})
else()
  if(CMAKE_CONFIGURATION_TYPES)
    list(GET CMAKE_CONFIGURATION_TYPES 0 CMAKE_TRY_COMPILE_CONFIGURATION)
  elseif(CMAKE_BUILD_TYPE)
    set(CMAKE_TRY_COMPILE_CONFIGURATION ${CMAKE_BUILD_TYPE})
  endif()
endif()

# NeverC monorepo: the only first-class project is `neverc` (compiler +
# embedded linker, formerly clang + lld). Historic sibling projects
# (bolt / clang-tools-extra / compiler-rt / flang / libc / libcxx / lldb /
# mlir / openmp / polly / pstl / ...) are no longer part of this tree, so
# the project lists collapse to a single entry.
set(LLVM_ALL_PROJECTS "neverc")
set(LLVM_KNOWN_PROJECTS "${LLVM_ALL_PROJECTS}")
set(LLVM_ENABLE_PROJECTS "neverc" CACHE STRING
    "Semicolon-separated list of projects to build (${LLVM_KNOWN_PROJECTS}), or \"all\".")
if(LLVM_ENABLE_PROJECTS STREQUAL "all")
  set(LLVM_ENABLE_PROJECTS ${LLVM_ALL_PROJECTS} CACHE STRING "" FORCE)
endif()
if(LLVM_ENABLE_PROJECTS STREQUAL "")
  message(STATUS "NeverC: forcing LLVM_ENABLE_PROJECTS=neverc")
  set(LLVM_ENABLE_PROJECTS "neverc" CACHE STRING "" FORCE)
endif()
list(REMOVE_DUPLICATES LLVM_ENABLE_PROJECTS)
set(LLVM_ENABLE_PROJECTS "${LLVM_ENABLE_PROJECTS}" CACHE STRING "" FORCE)
if(LLVM_EXTERNAL_PROJECTS)
  message(FATAL_ERROR
    "LLVM_EXTERNAL_PROJECTS is unsupported in NeverC; this tree only builds neverc.")
endif()
foreach(proj ${LLVM_ENABLE_PROJECTS})
  if(NOT "${proj}" IN_LIST LLVM_KNOWN_PROJECTS)
    message(FATAL_ERROR "${proj} isn't a known project: ${LLVM_KNOWN_PROJECTS}.")
  endif()
endforeach()

# Runtimes are no longer shipped in this tree either.
if(LLVM_ENABLE_RUNTIMES)
  message(FATAL_ERROR
    "LLVM_ENABLE_RUNTIMES=${LLVM_ENABLE_RUNTIMES} but no runtime projects "
    "are present in this tree. Remove the flag or vendor the runtime first.")
endif()

# NeverC is the only project in this monorepo.  Set the external-project knobs
# directly instead of carrying LLVM's historical multi-project cache state.
set(LLVM_ENABLE_PROJECTS_USED ON CACHE BOOL "")
mark_as_advanced(LLVM_ENABLE_PROJECTS_USED)
set(PROJ_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../neverc")
if(NOT EXISTS "${PROJ_DIR}" OR NOT IS_DIRECTORY "${PROJ_DIR}")
  message(FATAL_ERROR "NeverC project directory not found: ${PROJ_DIR}")
endif()
message(STATUS "neverc project is enabled")
set(LLVM_EXTERNAL_NEVERC_SOURCE_DIR "${PROJ_DIR}" CACHE PATH "" FORCE)
set(LLVM_TOOL_NEVERC_BUILD ON CACHE BOOL "Whether to build NEVERC as part of LLVM" FORCE)
unset(PROJ_DIR)

# Build llvm with ccache if the package is present
set(LLVM_CCACHE_BUILD OFF CACHE BOOL "Set to ON for a ccache enabled build")
set(LLVM_CCACHE_PROGRAM "" CACHE FILEPATH "Path to ccache or sccache")
if(LLVM_CCACHE_BUILD)
  if(NOT LLVM_CCACHE_PROGRAM)
    find_program(LLVM_CCACHE_PROGRAM NAMES ccache sccache)
  endif()
  if(LLVM_CCACHE_PROGRAM)
    get_filename_component(LLVM_CCACHE_PROGRAM_NAME
      "${LLVM_CCACHE_PROGRAM}" NAME)
    message(STATUS "Using compiler cache: ${LLVM_CCACHE_PROGRAM}")
    set(LLVM_CCACHE_MAXSIZE "" CACHE STRING "Size of ccache")
    set(LLVM_CCACHE_DIR "" CACHE STRING "Directory to keep ccached data")
    set(LLVM_CCACHE_PARAMS "CCACHE_CPP2=yes CCACHE_HASHDIR=yes"
        CACHE STRING "Parameters to pass through to ccache")

    if(LLVM_CCACHE_PROGRAM_NAME MATCHES "^ccache(\\.exe)?$")
      set(CCACHE_PROGRAM "${LLVM_CCACHE_PROGRAM}")
      if(NOT CMAKE_SYSTEM_NAME MATCHES "Windows")
        set(CCACHE_PROGRAM "${LLVM_CCACHE_PARAMS} ${CCACHE_PROGRAM}")
        if (LLVM_CCACHE_MAXSIZE)
          set(CCACHE_PROGRAM "CCACHE_MAXSIZE=${LLVM_CCACHE_MAXSIZE} ${CCACHE_PROGRAM}")
        endif()
        if (LLVM_CCACHE_DIR)
          set(CCACHE_PROGRAM "CCACHE_DIR=${LLVM_CCACHE_DIR} ${CCACHE_PROGRAM}")
        endif()
        set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ${CCACHE_PROGRAM})
      else()
        if(LLVM_CCACHE_MAXSIZE OR LLVM_CCACHE_DIR OR
           NOT LLVM_CCACHE_PARAMS MATCHES "CCACHE_CPP2=yes CCACHE_HASHDIR=yes")
          message(FATAL_ERROR "Ccache configuration through CMake is not supported on Windows. Please use environment variables.")
        endif()
        # RULE_LAUNCH_COMPILE should work with Ninja but currently has issues
        # with cmd.exe and some MSVC tools other than cl.exe
        set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
        set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
      endif()
    else()
      if (LLVM_CCACHE_MAXSIZE)
        message(WARNING "LLVM_CCACHE_MAXSIZE is only applied when LLVM_CCACHE_PROGRAM is ccache")
      endif()
      if (LLVM_CCACHE_DIR)
        message(WARNING "LLVM_CCACHE_DIR is only applied when LLVM_CCACHE_PROGRAM is ccache")
      endif()
      set(CMAKE_C_COMPILER_LAUNCHER "${LLVM_CCACHE_PROGRAM}")
      set(CMAKE_CXX_COMPILER_LAUNCHER "${LLVM_CCACHE_PROGRAM}")
    endif()
  else()
    message(FATAL_ERROR "Unable to find ccache or sccache. Set LLVM_CCACHE_BUILD to OFF")
  endif()
endif()

set(LLVM_EXTERNAL_PROJECT_BUILD_TOOL_ARGS "" CACHE STRING
  "Optional arguments for the native tool used in CMake --build invocations for external projects.")
mark_as_advanced(LLVM_EXTERNAL_PROJECT_BUILD_TOOL_ARGS)

option(LLVM_DEPENDENCY_DEBUGGING "Dependency debugging mode to verify correctly expressed library dependencies (Darwin only)" OFF)

# Some features of the LLVM build may be disallowed when dependency debugging is
# enabled. In particular you cannot use ccache because we want to force compile
# operations to always happen.
if(LLVM_DEPENDENCY_DEBUGGING)
  if(NOT CMAKE_HOST_APPLE)
    message(FATAL_ERROR "Dependency debugging is only currently supported on Darwin hosts.")
  endif()
  if(LLVM_CCACHE_BUILD)
    message(FATAL_ERROR "Cannot enable dependency debugging while using ccache.")
  endif()
endif()

option(LLVM_ENABLE_DAGISEL_COV "Debug: Prints tablegen patterns that were used for selecting" OFF)
option(LLVM_ENABLE_GISEL_COV "Enable collection of GlobalISel rule coverage" OFF)

# Add path for custom modules
list(INSERT CMAKE_MODULE_PATH 0
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake"
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules"
  "${LLVM_COMMON_CMAKE_UTILS}/Modules"
  )

# Generate a CompilationDatabase (compile_commands.json file) for our build,
# for use by clang_complete, YouCompleteMe, etc.
set(CMAKE_EXPORT_COMPILE_COMMANDS 1)

option(LLVM_INSTALL_BINUTILS_SYMLINKS
  "Install symlinks from the binutils tool names to the corresponding LLVM tools." OFF)

option(LLVM_INSTALL_CCTOOLS_SYMLINKS
  "Install symlinks from the cctools tool names to the corresponding LLVM tools." OFF)

# By default we use symlinks on Unix platforms and copy binaries on Windows
# If you have the correct setup on Windows you can use this option to enable
# symlinks and save a lot of diskspace.
option(LLVM_USE_SYMLINKS "Use symlinks instead of copying binaries" ${CMAKE_HOST_UNIX})

option(LLVM_INSTALL_UTILS "Include utility binaries in the 'install' target." OFF)

option(LLVM_INSTALL_TOOLCHAIN_ONLY "Only include toolchain files in the 'install' target." ON)

# Unfortunatly Clang is too eager to search directories for module maps, which can cause the
# installed version of the maps to be found when building LLVM from source. Therefore we turn off
# the installation by default. See llvm.org/PR31905.
option(LLVM_INSTALL_MODULEMAPS "Install the modulemap files in the 'install' target." OFF)

option(LLVM_USE_FOLDERS "Enable solution folders in Visual Studio. Disable for Express versions." ON)
if ( LLVM_USE_FOLDERS )
  set_property(GLOBAL PROPERTY USE_FOLDERS ON)
endif()

include(VersionFromVCS)

option(LLVM_APPEND_VC_REV
  "Embed the version control system revision in LLVM" OFF)

set(LLVM_FORCE_VC_REVISION
  "" CACHE STRING "Force custom VC revision for LLVM_APPEND_VC_REV")

set(LLVM_FORCE_VC_REPOSITORY
  "" CACHE STRING "Force custom VC repository for LLVM_APPEND_VC_REV")

option(LLVM_TOOL_LLVM_DRIVER_BUILD "Enables building the llvm multicall tool" OFF)

set(PACKAGE_NAME LLVM)
set(PACKAGE_STRING "${PACKAGE_NAME} ${PACKAGE_VERSION}")
set(PACKAGE_BUGREPORT "https://github.com/NeverSight/NeverC/issues")

set(BUG_REPORT_URL "${PACKAGE_BUGREPORT}" CACHE STRING
  "Default URL where bug reports are to be submitted.")

if(NOT LLVM_INSTALL_TOOLCHAIN_ONLY)
  # Configure CPack only for full distribution builds.
  if(NOT DEFINED CPACK_PACKAGE_INSTALL_DIRECTORY)
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "LLVM")
  endif()
  if(NOT DEFINED CPACK_PACKAGE_VENDOR)
    set(CPACK_PACKAGE_VENDOR "LLVM")
  endif()
  set(CPACK_PACKAGE_VERSION_MAJOR ${LLVM_VERSION_MAJOR})
  set(CPACK_PACKAGE_VERSION_MINOR ${LLVM_VERSION_MINOR})
  set(CPACK_PACKAGE_VERSION_PATCH ${LLVM_VERSION_PATCH})
  set(CPACK_PACKAGE_VERSION ${PACKAGE_VERSION})
  set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.TXT")
  include(CPack)
endif()

# Sanity check our source directory to make sure that we are not trying to
# generate an in-source build (unless on MSVC_IDE, where it is ok), and to make
# sure that we don't have any stray generated files lying around in the tree
# (which would end up getting picked up by header search, instead of the correct
# versions).
if( CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_CURRENT_BINARY_DIR AND NOT MSVC_IDE )
  message(FATAL_ERROR "In-source builds are not allowed.
Please create a directory and run cmake from there, passing the path
to this source directory as the last argument.
This process created the file `CMakeCache.txt' and the directory `CMakeFiles'.
Please delete them.")
endif()

string(TOUPPER "${CMAKE_BUILD_TYPE}" uppercase_CMAKE_BUILD_TYPE)

option(LLVM_ADDITIONAL_BUILD_TYPES "Additional build types that are allowed to be passed into CMAKE_BUILD_TYPE" "")

set(ALLOWED_BUILD_TYPES DEBUG RELEASE RELWITHDEBINFO MINSIZEREL ${LLVM_ADDITIONAL_BUILD_TYPES})
string (REPLACE ";" "|" ALLOWED_BUILD_TYPES_STRING "${ALLOWED_BUILD_TYPES}")
string (TOUPPER "${ALLOWED_BUILD_TYPES_STRING}" uppercase_ALLOWED_BUILD_TYPES)

if (CMAKE_BUILD_TYPE AND
    NOT uppercase_CMAKE_BUILD_TYPE MATCHES "^(${uppercase_ALLOWED_BUILD_TYPES})$")
  message(FATAL_ERROR "Unknown value for CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")
endif()

# LLVM_INSTALL_PACKAGE_DIR is shared by the generated LLVM/Clang CMake package
# files used from the build and install trees.
include(GNUInstallPackageDir)
set(LLVM_INSTALL_PACKAGE_DIR "${CMAKE_INSTALL_PACKAGEDIR}/llvm" CACHE STRING
  "Path for CMake subdirectory for LLVM (defaults to '${CMAKE_INSTALL_PACKAGEDIR}/llvm')")

set(LLVM_TOOLS_INSTALL_DIR "${CMAKE_INSTALL_BINDIR}" CACHE STRING
    "Path for binary subdirectory (defaults to '${CMAKE_INSTALL_BINDIR}')")
mark_as_advanced(LLVM_TOOLS_INSTALL_DIR)

set(LLVM_UTILS_INSTALL_DIR "${LLVM_TOOLS_INSTALL_DIR}" CACHE STRING
    "Path to install LLVM utilities (enabled by LLVM_INSTALL_UTILS=ON) (defaults to LLVM_TOOLS_INSTALL_DIR)")
mark_as_advanced(LLVM_UTILS_INSTALL_DIR)

set(LLVM_EXAMPLES_INSTALL_DIR "examples" CACHE STRING
    "Path for examples subdirectory (enabled by LLVM_BUILD_EXAMPLES=ON) (defaults to 'examples')")
mark_as_advanced(LLVM_EXAMPLES_INSTALL_DIR)

# They are used as destination of target generators.
set(LLVM_RUNTIME_OUTPUT_INTDIR ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}/bin)
set(LLVM_LIBRARY_OUTPUT_INTDIR ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}/lib${LLVM_LIBDIR_SUFFIX})
if(WIN32 OR CYGWIN)
  # DLL platform -- put DLLs into bin.
  set(LLVM_SHLIB_OUTPUT_INTDIR ${LLVM_RUNTIME_OUTPUT_INTDIR})
else()
  set(LLVM_SHLIB_OUTPUT_INTDIR ${LLVM_LIBRARY_OUTPUT_INTDIR})
endif()

# Each of them corresponds to llvm-config's.
set(LLVM_TOOLS_BINARY_DIR ${LLVM_RUNTIME_OUTPUT_INTDIR}) # --bindir
set(LLVM_LIBRARY_DIR      ${LLVM_LIBRARY_OUTPUT_INTDIR}) # --libdir
set(LLVM_MAIN_SRC_DIR     ${CMAKE_CURRENT_SOURCE_DIR}  ) # --src-root
set(LLVM_MAIN_INCLUDE_DIR ${LLVM_MAIN_SRC_DIR}/include ) # --includedir
set(LLVM_BINARY_DIR       ${CMAKE_CURRENT_BINARY_DIR}  ) # --prefix


# Note: LLVM_CMAKE_DIR does not include generated files
set(LLVM_CMAKE_DIR ${LLVM_MAIN_SRC_DIR}/cmake/modules)
set(LLVM_EXAMPLES_BINARY_DIR ${LLVM_BINARY_DIR}/examples)
set(LLVM_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/include)

# Only x86/x64 and AArch64.
set(LLVM_ALL_TARGETS
  AArch64
  X86
  )

set(LLVM_ALL_EXPERIMENTAL_TARGETS
  )

# List of targets with JIT support:
set(LLVM_TARGETS_WITH_JIT X86 AArch64)

set(LLVM_TARGETS_TO_BUILD "AArch64;X86"
    CACHE STRING "Semicolon-separated list of targets to build, or \"all\".")

set(LLVM_EXPERIMENTAL_TARGETS_TO_BUILD ""
    CACHE STRING "Semicolon-separated list of experimental targets to build, or \"all\".")

option(BUILD_SHARED_LIBS
  "Build all libraries as shared libraries instead of static" OFF)

option(LLVM_ENABLE_BACKTRACES "Enable embedding backtraces on crash." OFF)
if(LLVM_ENABLE_BACKTRACES)
  set(ENABLE_BACKTRACES 1)
endif()

option(LLVM_ENABLE_UNWIND_TABLES "Emit unwind tables for the libraries" ON)

option(LLVM_ENABLE_CRASH_OVERRIDES "Enable crash overrides." ON)
if(LLVM_ENABLE_CRASH_OVERRIDES)
  set(ENABLE_CRASH_OVERRIDES 1)
endif()

option(LLVM_ENABLE_CRASH_DUMPS "Turn on memory dumps on crashes. Currently only implemented on Windows." OFF)

set(WINDOWS_PREFER_FORWARD_SLASH_DEFAULT OFF)
if (MINGW)
  # Cygwin doesn't identify itself as Windows, and thus gets path::Style::posix
  # as native path style, regardless of what this is set to.
  set(WINDOWS_PREFER_FORWARD_SLASH_DEFAULT ON)
endif()
option(LLVM_WINDOWS_PREFER_FORWARD_SLASH "Prefer path names with forward slashes on Windows." ${WINDOWS_PREFER_FORWARD_SLASH_DEFAULT})

set(LLVM_ENABLE_FFI OFF)

set(LLVM_TARGET_ARCH "host"
  CACHE STRING "Set target to use for LLVM JIT or use \"host\" for automatic detection.")

set(LLVM_ENABLE_TERMINFO OFF)
set(LLVM_ENABLE_LIBEDIT OFF)

# On z/OS, threads cannot be used because TLS is not supported.
if (CMAKE_SYSTEM_NAME MATCHES "OS390")
  option(LLVM_ENABLE_THREADS "Use threads if available." OFF)
else()
  option(LLVM_ENABLE_THREADS "Use threads if available." ON)
endif()

set(LLVM_ENABLE_ZLIB OFF)
set(LLVM_ENABLE_ZSTD OFF)
set(LLVM_USE_STATIC_ZSTD FALSE)

if( LLVM_TARGETS_TO_BUILD STREQUAL "all" )
  set( LLVM_TARGETS_TO_BUILD ${LLVM_ALL_TARGETS} )
endif()

if(LLVM_EXPERIMENTAL_TARGETS_TO_BUILD STREQUAL "all")
  set(LLVM_EXPERIMENTAL_TARGETS_TO_BUILD ${LLVM_ALL_EXPERIMENTAL_TARGETS})
endif()

set(LLVM_TARGETS_TO_BUILD
   ${LLVM_TARGETS_TO_BUILD}
   ${LLVM_EXPERIMENTAL_TARGETS_TO_BUILD})
list(REMOVE_DUPLICATES LLVM_TARGETS_TO_BUILD)

if (NOT CMAKE_SYSTEM_NAME MATCHES "OS390")
  option(LLVM_ENABLE_PIC "Build Position-Independent Code" ON)
endif()
option(LLVM_ENABLE_MODULES "Compile with C++ modules enabled." OFF)
if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  option(LLVM_ENABLE_MODULE_DEBUGGING "Compile with -gmodules." ON)
else()
  option(LLVM_ENABLE_MODULE_DEBUGGING "Compile with -gmodules." OFF)
endif()
option(LLVM_ENABLE_LOCAL_SUBMODULE_VISIBILITY "Compile with -fmodules-local-submodule-visibility." ON)
option(LLVM_ENABLE_LIBCXX "Use libc++ if available." OFF)
option(LLVM_ENABLE_LLVM_LIBC "Set to on to link all LLVM executables against LLVM libc, assuming it is accessible by the host compiler." OFF)
option(LLVM_STATIC_LINK_CXX_STDLIB "Statically link the standard library." OFF)
option(LLVM_ENABLE_LLD "Use lld as C and C++ linker." OFF)

option(LLVM_ENABLE_PEDANTIC "Compile with pedantic enabled." ON)
option(LLVM_ENABLE_WERROR "Fail and stop if a warning is triggered." OFF)

option(LLVM_ENABLE_DUMP "Enable dump functions even when assertions are disabled" OFF)
option(LLVM_UNREACHABLE_OPTIMIZE "Optimize llvm_unreachable() as undefined behavior (default), guaranteed trap when OFF" ON)

if( NOT uppercase_CMAKE_BUILD_TYPE STREQUAL "DEBUG" )
  option(LLVM_ENABLE_ASSERTIONS "Enable assertions" OFF)
else()
  option(LLVM_ENABLE_ASSERTIONS "Enable assertions" ON)
endif()

option(LLVM_ENABLE_EXPENSIVE_CHECKS "Enable expensive checks" OFF)

# While adding scalable vector support to LLVM, we temporarily want to
# allow an implicit conversion of TypeSize to uint64_t, and to allow
# code to get the fixed number of elements from a possibly scalable vector.
# This CMake flag enables a more strict mode where it asserts that the type
# is not a scalable vector type.
#
# Enabling this flag makes it easier to find cases where the compiler makes
# assumptions on the size being 'fixed size', when building tests for
# SVE/SVE2 or other scalable vector architectures.
option(LLVM_ENABLE_STRICT_FIXED_SIZE_VECTORS
       "Enable assertions that type is not scalable in implicit conversion from TypeSize to uint64_t and calls to getNumElements" OFF)

set(LLVM_ABI_BREAKING_CHECKS "WITH_ASSERTS" CACHE STRING
  "Enable abi-breaking checks.  Can be WITH_ASSERTS, FORCE_ON or FORCE_OFF.")

option(LLVM_FORCE_USE_OLD_TOOLCHAIN
       "Set to ON to force using an old, unsupported host toolchain." OFF)

set(LLVM_LOCAL_RPATH "" CACHE FILEPATH
  "If set, an absolute path added as rpath on binaries that do not already contain an executable-relative rpath.")

option(LLVM_TEMPORARILY_ALLOW_OLD_TOOLCHAIN
       "Set to ON to only warn when using a toolchain which is about to be deprecated, instead of emitting an error." OFF)

set(LLVM_USE_INTEL_JITEVENTS OFF)
set(LLVM_USE_OPROFILE OFF)
set(LLVM_USE_PERF OFF)

option(LLVM_EXTERNALIZE_DEBUGINFO
  "Generate dSYM files and strip executables and libraries (Darwin Only)" OFF)

option(LLVM_EXPERIMENTAL_DEBUGINFO_ITERATORS
  "Add extra Booleans to ilist_iterators to communicate facts for debug-info" OFF)

set(LLVM_CODESIGNING_IDENTITY "" CACHE STRING
  "Sign executables and dylibs with the given identity or skip if empty (Darwin Only)")

set(LLVM_USE_SANITIZER "" CACHE STRING
  "Define the sanitizer used to build binaries and tests.")
option(LLVM_OPTIMIZE_SANITIZED_BUILDS "Pass -O1 on debug sanitizer builds" ON)
set(LLVM_UBSAN_FLAGS
    "-fsanitize=undefined -fno-sanitize=vptr,function -fno-sanitize-recover=all"
    CACHE STRING
    "Compile flags set to enable UBSan. Only used if LLVM_USE_SANITIZER contains 'Undefined'.")
set(LLVM_LIB_FUZZING_ENGINE "" CACHE PATH
  "Path to fuzzing library for linking with fuzz targets")

option(LLVM_USE_SPLIT_DWARF
  "Use -gsplit-dwarf when compiling llvm and --gdb-index when linking." OFF)

# Define an option controlling whether we should build for 32-bit on 64-bit
# platforms, where supported.
if( CMAKE_SIZEOF_VOID_P EQUAL 8 AND NOT (WIN32 OR ${CMAKE_SYSTEM_NAME} MATCHES "AIX"))
  # TODO: support other platforms and toolchains.
  option(LLVM_BUILD_32_BITS "Build 32 bits executables and libraries." OFF)
endif()

if(LLVM_INCLUDE_TESTS)
  # Define the default arguments to use with 'lit', and an option for the user to
  # override.
  set(LIT_ARGS_DEFAULT "-sv")
  if (MSVC_IDE OR XCODE)
    set(LIT_ARGS_DEFAULT "${LIT_ARGS_DEFAULT} --no-progress-bar")
  endif()
  if(LLVM_INDIVIDUAL_TEST_COVERAGE)
     set(LIT_ARGS_DEFAULT "${LIT_ARGS_DEFAULT} --per-test-coverage")
  endif()
  set(LLVM_LIT_ARGS "${LIT_ARGS_DEFAULT}" CACHE STRING "Default options for lit")

  # On Win32 hosts, provide an option to specify the path to the GnuWin32 tools.
  if( WIN32 AND NOT CYGWIN )
    set(LLVM_LIT_TOOLS_DIR "" CACHE PATH "Path to GnuWin32 tools")
  endif()
endif()
set(LLVM_NATIVE_TOOL_DIR "" CACHE PATH "Path to a directory containing prebuilt matching native tools")

# Define options to control the inclusion and default build behavior for
# components which may not strictly be necessary (tools, examples, and tests).
#
# This is primarily to support building smaller or faster project files.
option(LLVM_INCLUDE_TOOLS
  "Enable optional LLVM tool-related configure checks; NeverC does not ship LLVM tools." OFF)
option(LLVM_BUILD_TOOLS
  "Build the LLVM tools. If OFF, just generate build targets." OFF)

option(LLVM_BUILD_UTILS
  "Build LLVM utility binaries. If OFF, just generate build targets." OFF)

option(LLVM_BUILD_EXAMPLES
  "Build the LLVM example programs. If OFF, just generate build targets." OFF)
option(LLVM_INCLUDE_EXAMPLES "Generate build targets for the LLVM examples" OFF)

if(LLVM_BUILD_EXAMPLES)
  add_compile_definitions(BUILD_EXAMPLES)
endif(LLVM_BUILD_EXAMPLES)

option(LLVM_BUILD_TESTS
  "Build LLVM unit tests. If OFF, just generate build targets." OFF)
option(LLVM_INCLUDE_TESTS "Generate build targets for the LLVM unit tests." OFF)

option(LLVM_INSTALL_GTEST
  "Install the llvm gtest library.  This should be on if you want to do
   stand-alone builds of the other projects and run their unit tests." OFF)

set(LLVM_BUILD_BENCHMARKS OFF)
set(LLVM_INCLUDE_BENCHMARKS OFF)

set(LLVM_BUILD_DOCS OFF)
set(LLVM_INCLUDE_DOCS OFF)
set(LLVM_ENABLE_DOXYGEN OFF)
set(LLVM_ENABLE_SPHINX OFF)

set(LLVM_BUILD_EXTERNAL_COMPILER_RT OFF)

option (LLVM_VERSION_PRINTER_SHOW_HOST_TARGET_INFO
  "Show target and host info when tools are invoked with --version." ON)

# You can configure which libraries from LLVM you want to include in the
# shared library by setting LLVM_DYLIB_COMPONENTS to a semi-colon delimited
# list of LLVM components. All component names handled by llvm-config are valid.
if(NOT DEFINED LLVM_DYLIB_COMPONENTS)
  set(LLVM_DYLIB_COMPONENTS "all" CACHE STRING
    "Semicolon-separated list of components to include in libLLVM, or \"all\".")
endif()

if(MSVC)
  option(LLVM_BUILD_LLVM_C_DYLIB "Build LLVM-C.dll (Windows only)" ON)
  # Set this variable to OFF here so it can't be set with a command-line
  # argument.
  set (LLVM_LINK_LLVM_DYLIB OFF)
  if (BUILD_SHARED_LIBS)
    message(FATAL_ERROR "BUILD_SHARED_LIBS options is not supported on Windows.")
  endif()
else()
  option(LLVM_LINK_LLVM_DYLIB "Link tools against the libllvm dynamic library" OFF)
  option(LLVM_BUILD_LLVM_C_DYLIB "Build libllvm-c re-export library (Darwin only)" OFF)
  set(LLVM_BUILD_LLVM_DYLIB_default OFF)
  if(LLVM_LINK_LLVM_DYLIB OR LLVM_BUILD_LLVM_C_DYLIB)
    set(LLVM_BUILD_LLVM_DYLIB_default ON)
  endif()
  option(LLVM_BUILD_LLVM_DYLIB "Build libllvm dynamic library" ${LLVM_BUILD_LLVM_DYLIB_default})
endif()

if (LLVM_LINK_LLVM_DYLIB AND BUILD_SHARED_LIBS)
  message(FATAL_ERROR "Cannot enable BUILD_SHARED_LIBS with LLVM_LINK_LLVM_DYLIB.  We recommend disabling BUILD_SHARED_LIBS.")
endif()


if (MSVC_IDE)
  option(LLVM_ADD_NATIVE_VISUALIZERS_TO_SOLUTION "Configure project to use Visual Studio native visualizers" TRUE)
endif()

if(NOT LLVM_INDIVIDUAL_TEST_COVERAGE)
  if(LLVM_BUILD_INSTRUMENTED OR LLVM_BUILD_INSTRUMENTED_COVERAGE OR LLVM_ENABLE_IR_PGO)
    if(NOT LLVM_PROFILE_MERGE_POOL_SIZE)
      # A pool size of 1-2 is probably sufficient on an SSD. 3-4 should be fine
      # for spinning disks. Anything higher may only help on slower mediums.
      set(LLVM_PROFILE_MERGE_POOL_SIZE "4")
    endif()
    if(NOT LLVM_PROFILE_FILE_PATTERN)
      if(NOT LLVM_PROFILE_DATA_DIR)
        file(TO_NATIVE_PATH "${LLVM_BINARY_DIR}/profiles" LLVM_PROFILE_DATA_DIR)
      endif()
      file(TO_NATIVE_PATH "${LLVM_PROFILE_DATA_DIR}/%${LLVM_PROFILE_MERGE_POOL_SIZE}m.profraw" LLVM_PROFILE_FILE_PATTERN)
    endif()
    if(NOT LLVM_CSPROFILE_FILE_PATTERN)
      if(NOT LLVM_CSPROFILE_DATA_DIR)
        file(TO_NATIVE_PATH "${LLVM_BINARY_DIR}/csprofiles" LLVM_CSPROFILE_DATA_DIR)
      endif()
      file(TO_NATIVE_PATH "${LLVM_CSPROFILE_DATA_DIR}/%${LLVM_PROFILE_MERGE_POOL_SIZE}m.profraw" LLVM_CSPROFILE_FILE_PATTERN)
    endif()
  endif()
endif()

if (LLVM_BUILD_STATIC)
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
  # Remove shared library suffixes from use in find_library
  foreach (shared_lib_suffix ${CMAKE_SHARED_LIBRARY_SUFFIX} ${CMAKE_IMPORT_LIBRARY_SUFFIX})
    list(FIND CMAKE_FIND_LIBRARY_SUFFIXES ${shared_lib_suffix} shared_lib_suffix_idx)
    if(NOT ${shared_lib_suffix_idx} EQUAL -1)
      list(REMOVE_AT CMAKE_FIND_LIBRARY_SUFFIXES ${shared_lib_suffix_idx})
    endif()
  endforeach()
endif()

# Use libtool instead of ar if you are both on an Apple host, and targeting Apple.
# NeverC may set NEVERC_USE_LLVM_AR when building with upstream LLVM clang.
if(CMAKE_HOST_APPLE AND APPLE AND NOT NEVERC_USE_LLVM_AR)
  include(UseLibtool)
endif()

# Override the default target with an environment variable named by LLVM_TARGET_TRIPLE_ENV.
set(LLVM_TARGET_TRIPLE_ENV CACHE STRING "The name of environment variable to override default target. Disabled by blank.")
mark_as_advanced(LLVM_TARGET_TRIPLE_ENV)

if(CMAKE_SYSTEM_NAME MATCHES "BSD|Linux|OS390")
  set(LLVM_ENABLE_PER_TARGET_RUNTIME_DIR_default ON)
else()
  set(LLVM_ENABLE_PER_TARGET_RUNTIME_DIR_default OFF)
endif()
set(LLVM_ENABLE_PER_TARGET_RUNTIME_DIR ${LLVM_ENABLE_PER_TARGET_RUNTIME_DIR_default} CACHE BOOL
  "Enable per-target runtimes directory")

set(LLVM_PROFDATA_FILE "" CACHE FILEPATH
  "Profiling data file to use when compiling in order to improve runtime performance.")

if(LLVM_INCLUDE_TESTS)
  # Lit test suite requires at least python 3.6
  set(LLVM_MINIMUM_PYTHON_VERSION 3.6)
else()
  # FIXME: it is unknown if this is the actual minimum bound
  set(LLVM_MINIMUM_PYTHON_VERSION 3.0)
endif()

# config-ix only needs Python when optional tool/doc paths ask it to probe
# Python modules.  The default NeverC toolchain build does not use Python.
if(LLVM_INCLUDE_TESTS OR LLVM_INCLUDE_TOOLS OR LLVM_INCLUDE_DOCS OR
   LLVM_BUILD_DOCS OR LLVM_BUILD_INSTRUMENTED_COVERAGE)
  find_package(Python3 ${LLVM_MINIMUM_PYTHON_VERSION} REQUIRED
    COMPONENTS Interpreter)
endif()

# All options referred to from HandleLLVMOptions have to be specified
# BEFORE this include, otherwise options will not be correctly set on
# first cmake run
include(config-ix)

# By default, we target the host, but this can be overridden at CMake
# invocation time. Except on 64-bit AIX, where the system toolchain
# expect 32-bit objects by default.
if("${LLVM_HOST_TRIPLE}" MATCHES "^powerpc64-ibm-aix")
  string(REGEX REPLACE "^powerpc64" "powerpc" LLVM_DEFAULT_TARGET_TRIPLE_DEFAULT "${LLVM_HOST_TRIPLE}")
else()
  # Only set default triple when native target is enabled.
  if (LLVM_NATIVE_TARGET)
    set(LLVM_DEFAULT_TARGET_TRIPLE_DEFAULT "${LLVM_HOST_TRIPLE}")
  endif()
endif()

set(LLVM_DEFAULT_TARGET_TRIPLE "${LLVM_DEFAULT_TARGET_TRIPLE_DEFAULT}" CACHE STRING
    "Default target for which LLVM will generate code." )
message(STATUS "LLVM default target triple: ${LLVM_DEFAULT_TARGET_TRIPLE}")

set(LLVM_TARGET_TRIPLE "${LLVM_DEFAULT_TARGET_TRIPLE}")

set(LLVM_ENABLE_PLUGINS_default OFF)
option(LLVM_ENABLE_PLUGINS "Enable plugin support" ${LLVM_ENABLE_PLUGINS_default})

set(LLVM_ENABLE_NEW_PASS_MANAGER TRUE CACHE BOOL
  "Enable the new pass manager by default.")
if(NOT LLVM_ENABLE_NEW_PASS_MANAGER)
  message(FATAL_ERROR "Enabling the legacy pass manager on the cmake level is"
                      " no longer supported.")
endif()

include(HandleLLVMOptions)

######

# Configure all of the various header file fragments LLVM uses which depend on
# configuration variables.
set(LLVM_ENUM_TARGETS "")
set(LLVM_ENUM_ASM_PRINTERS "")
set(LLVM_ENUM_ASM_PARSERS "")
set(LLVM_ENUM_DISASSEMBLERS "")
set(LLVM_ENUM_TARGETMCAS "")
foreach(t ${LLVM_TARGETS_TO_BUILD})
  # Make sure that any experimental targets were passed via
  # LLVM_EXPERIMENTAL_TARGETS_TO_BUILD, not LLVM_TARGETS_TO_BUILD.
  # We allow experimental targets that are not in LLVM_ALL_EXPERIMENTAL_TARGETS,
  # as long as they are passed via LLVM_EXPERIMENTAL_TARGETS_TO_BUILD.
  if ( NOT "${t}" IN_LIST LLVM_ALL_TARGETS AND NOT "${t}" IN_LIST LLVM_EXPERIMENTAL_TARGETS_TO_BUILD )
    if( "${t}" IN_LIST LLVM_ALL_EXPERIMENTAL_TARGETS )
      message(FATAL_ERROR "The target `${t}' is experimental and must be passed "
        "via LLVM_EXPERIMENTAL_TARGETS_TO_BUILD.")
    else()
      message(FATAL_ERROR "The target `${t}' is not a core tier target. It may be "
        "experimental, if so it must be passed via "
        "LLVM_EXPERIMENTAL_TARGETS_TO_BUILD.\n"
        "Core tier targets: ${LLVM_ALL_TARGETS}\n"
        "Known experimental targets: ${LLVM_ALL_EXPERIMENTAL_TARGETS}")
    endif()
  else()
    set(LLVM_ENUM_TARGETS "${LLVM_ENUM_TARGETS}LLVM_TARGET(${t})\n")
    string(TOUPPER ${t} T_UPPER)
    set(LLVM_HAS_${T_UPPER}_TARGET 1)
  endif()

  # NeverC only ships AArch64 and X86.  Both targets provide asm printers and
  # parsers; neither ships disassembler or MCA components in this tree.
  set(LLVM_ENUM_ASM_PRINTERS
    "${LLVM_ENUM_ASM_PRINTERS}LLVM_ASM_PRINTER(${t})\n")
  set(LLVM_ENUM_ASM_PARSERS
    "${LLVM_ENUM_ASM_PARSERS}LLVM_ASM_PARSER(${t})\n")
endforeach(t)

# Provide an LLVM_ namespaced alias for use in #cmakedefine.
set(LLVM_BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS})

# Produce the target definition files, which provide a way for clients to easily
# include various classes of targets.
configure_file(
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Config/AsmPrinters.def.in
  ${LLVM_INCLUDE_DIR}/llvm/Config/AsmPrinters.def
  )
configure_file(
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Config/AsmParsers.def.in
  ${LLVM_INCLUDE_DIR}/llvm/Config/AsmParsers.def
  )
configure_file(
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Config/Disassemblers.def.in
  ${LLVM_INCLUDE_DIR}/llvm/Config/Disassemblers.def
  )
configure_file(
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Config/Targets.def.in
  ${LLVM_INCLUDE_DIR}/llvm/Config/Targets.def
  )
configure_file(
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Config/TargetMCAs.def.in
  ${LLVM_INCLUDE_DIR}/llvm/Config/TargetMCAs.def
  )
# They are not referenced. See set_output_directory().
set( CMAKE_RUNTIME_OUTPUT_DIRECTORY ${LLVM_TOOLS_BINARY_DIR} )
set( CMAKE_LIBRARY_OUTPUT_DIRECTORY ${LLVM_LIBRARY_DIR} )
set( CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${LLVM_LIBRARY_DIR} )

# Configure the three LLVM configuration header files.
configure_file(
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Config/config.h.cmake
  ${LLVM_INCLUDE_DIR}/llvm/Config/config.h)
configure_file(
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Config/llvm-config.h.cmake
  ${LLVM_INCLUDE_DIR}/llvm/Config/llvm-config.h)
configure_file(
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Config/abi-breaking.h.cmake
  ${LLVM_INCLUDE_DIR}/llvm/Config/abi-breaking.h)


if(APPLE AND DARWIN_LTO_LIBRARY)
  set(CMAKE_EXE_LINKER_FLAGS
    "${CMAKE_EXE_LINKER_FLAGS} -Wl,-lto_library -Wl,${DARWIN_LTO_LIBRARY}")
  set(CMAKE_SHARED_LINKER_FLAGS
    "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-lto_library -Wl,${DARWIN_LTO_LIBRARY}")
  set(CMAKE_MODULE_LINKER_FLAGS
    "${CMAKE_MODULE_LINKER_FLAGS} -Wl,-lto_library -Wl,${DARWIN_LTO_LIBRARY}")
endif()

# Build with _XOPEN_SOURCE on AIX, as stray macros in _ALL_SOURCE mode tend to
# break things. In this case we need to enable the large-file API as well.
if (UNIX AND ${CMAKE_SYSTEM_NAME} MATCHES "AIX")
          add_compile_definitions(_XOPEN_SOURCE=700)
          add_compile_definitions(_LARGE_FILE_API)

  # Modules should be built with -shared -Wl,-G, so we can use runtime linking
  # with plugins.
  string(APPEND CMAKE_MODULE_LINKER_FLAGS " -shared -Wl,-G")

  # Also set the correct flags for building shared libraries.
  string(APPEND CMAKE_SHARED_LINKER_FLAGS " -shared")
endif()

# Build with _XOPEN_SOURCE on z/OS.
if (CMAKE_SYSTEM_NAME MATCHES "OS390")
  add_compile_definitions(_XOPEN_SOURCE=600)
  add_compile_definitions(_OPEN_SYS) # Needed for process information.
  add_compile_definitions(_OPEN_SYS_FILE_EXT) # Needed for EBCDIC I/O.
  add_compile_definitions(_EXT) # Needed for file data.
  add_compile_definitions(_UNIX03_THREADS) # Multithreading support.
endif()

# Build with _FILE_OFFSET_BITS=64 on Solaris to match g++ >= 9.
if (UNIX AND ${CMAKE_SYSTEM_NAME} MATCHES "SunOS")
          add_compile_definitions(_FILE_OFFSET_BITS=64)
endif()

set(CMAKE_INCLUDE_CURRENT_DIR ON)

include_directories( ${LLVM_INCLUDE_DIR} ${LLVM_MAIN_INCLUDE_DIR})



# Make sure we don't get -rdynamic in every binary. For those that need it,
# use export_executable_symbols(target).
set(CMAKE_SHARED_LIBRARY_LINK_CXX_FLAGS "")

include(AddLLVM)

include(LLVMDistributionSupport)

# Define linker backend switches before entering llvm/lib so component
# directories needed only by optional backends can be skipped early.
option(LINKER_ENABLE_COFF  "Build COFF linker backend (linkerCOFF)."    ON)
option(LINKER_ENABLE_ELF   "Build ELF linker backend (linkerELF)."      ON)
option(LINKER_ENABLE_MACHO "Build Mach-O linker backend (linkerMachO)." ON)

if( MINGW AND NOT "${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang" )
  # People report that -O3 is unreliable on MinGW. The traditional
  # build also uses -O2 for that reason:
  llvm_replace_compiler_option(CMAKE_CXX_FLAGS_RELEASE "-O3" "-O2")
endif()

if(LLVM_INCLUDE_TESTS)
  umbrella_lit_testsuite_begin(check-all)
endif()

add_subdirectory(lib/CSupport)

add_subdirectory(include)

# NeverC only needs the LLVM libraries reached through the neverc executable
# dependency graph. Keep the component targets available, but don't put every
# LLVM library in the default "all" build.
add_subdirectory(lib EXCLUDE_FROM_ALL)

# NeverC is the only supported tool project in this tree. Wire it directly
# instead of traversing llvm/tools and generating generic LLVM tool options.
add_llvm_external_project(neverc)

if( LLVM_INCLUDE_EXAMPLES AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples" )
  add_subdirectory(examples)
endif()

if( LLVM_INCLUDE_TESTS )
  set(LLVM_GTEST_RUN_UNDER
    "" CACHE STRING
    "Define the wrapper program that LLVM unit tests should be run under.")
  if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/utils/lit")
    add_subdirectory(utils/lit)
  endif()
  if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test")
    add_subdirectory(test)
  endif()
  if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/unittests")
    add_subdirectory(unittests)
  endif()

  if (WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/utils/KillTheDoctor")
    add_subdirectory(utils/KillTheDoctor)
  endif()

  umbrella_lit_testsuite_end(check-all)
  get_property(LLVM_ALL_LIT_DEPENDS GLOBAL PROPERTY LLVM_ALL_LIT_DEPENDS)
  get_property(LLVM_ALL_ADDITIONAL_TEST_DEPENDS
      GLOBAL PROPERTY LLVM_ALL_ADDITIONAL_TEST_DEPENDS)
  add_custom_target(test-depends
      DEPENDS ${LLVM_ALL_LIT_DEPENDS} ${LLVM_ALL_ADDITIONAL_TEST_DEPENDS})
  set_target_properties(test-depends PROPERTIES FOLDER "Tests")
  add_dependencies(check-all test-depends)
endif()

if (LLVM_INCLUDE_DOCS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/docs")
  add_subdirectory(docs)
endif()

if(NOT LLVM_INSTALL_TOOLCHAIN_ONLY)
  add_subdirectory(cmake/modules)
endif()

if (NOT LLVM_INSTALL_TOOLCHAIN_ONLY)
  install(DIRECTORY include/llvm include/llvm-c
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    COMPONENT llvm-headers
    FILES_MATCHING
    PATTERN "*.def"
    PATTERN "*.h"
    PATTERN "*.td"
    PATTERN "*.inc"
    PATTERN "LICENSE.TXT"
    )

  install(DIRECTORY ${LLVM_INCLUDE_DIR}/llvm ${LLVM_INCLUDE_DIR}/llvm-c
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    COMPONENT llvm-headers
    FILES_MATCHING
    PATTERN "*.def"
    PATTERN "*.h"
    PATTERN "*.gen"
    PATTERN "*.inc"
    # Exclude include/llvm/CMakeFiles/intrinsics_gen.dir, matched by "*.def"
    PATTERN "CMakeFiles" EXCLUDE
    PATTERN "config.h" EXCLUDE
    )

  if (LLVM_INSTALL_MODULEMAPS)
    install(DIRECTORY include
            DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
            COMPONENT llvm-headers
            FILES_MATCHING
            PATTERN "module.modulemap"
            )
    install(FILES include/module.install.modulemap
            DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
            COMPONENT llvm-headers
            RENAME "module.extern.modulemap"
            )
  endif(LLVM_INSTALL_MODULEMAPS)

  # Installing the headers needs to depend on generating any public
  # tablegen'd headers.
  add_custom_target(llvm-headers DEPENDS intrinsics_gen)
  set_target_properties(llvm-headers PROPERTIES FOLDER "Misc")

  if (NOT LLVM_ENABLE_IDE)
    add_llvm_install_targets(install-llvm-headers
                             DEPENDS llvm-headers
                             COMPONENT llvm-headers)
  endif()

  # Custom target to install all libraries.
  add_custom_target(llvm-libraries)
  set_target_properties(llvm-libraries PROPERTIES FOLDER "Misc")

  if (NOT LLVM_ENABLE_IDE)
    add_llvm_install_targets(install-llvm-libraries
                             DEPENDS llvm-libraries
                             COMPONENT llvm-libraries)
  endif()

  get_property(LLVM_LIBS GLOBAL PROPERTY LLVM_LIBS)
  if(LLVM_LIBS)
    list(REMOVE_DUPLICATES LLVM_LIBS)
    foreach(lib ${LLVM_LIBS})
      add_dependencies(llvm-libraries ${lib})
      if (NOT LLVM_ENABLE_IDE)
        add_dependencies(install-llvm-libraries install-${lib})
        add_dependencies(install-llvm-libraries-stripped install-${lib}-stripped)
      endif()
    endforeach()
  endif()
endif()

# This must be at the end of the LLVM root CMakeLists file because it must run
# after all targets are created.
if(NOT LLVM_INSTALL_TOOLCHAIN_ONLY)
  llvm_distribution_add_targets()
endif()
if(LLVM_ENABLE_PLUGINS OR LLVM_EXPORT_SYMBOLS_FOR_PLUGINS)
  process_llvm_pass_plugins(GEN_CONFIG)
endif()

# This allows us to deploy the Universal CRT DLLs by passing -DCMAKE_INSTALL_UCRT_LIBRARIES=ON to CMake
if (MSVC AND CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows" AND CMAKE_INSTALL_UCRT_LIBRARIES)
  include(InstallRequiredSystemLibraries)
endif()


