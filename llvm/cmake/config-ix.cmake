if( WIN32 AND NOT CYGWIN )
  # We consider Cygwin as another Unix
  set(PURE_WINDOWS 1)
endif()

include(CheckIncludeFile)
include(CheckSymbolExists)
include(CheckCXXSymbolExists)
include(CheckFunctionExists)
include(CheckStructHasMember)
include(CheckCSourceCompiles)
include(CMakePushCheckState)

include(CheckCompilerVersion)
include(CheckProblematicConfigurations)
include(HandleLLVMStdlib)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  # Used by check_symbol_exists:
  list(APPEND CMAKE_REQUIRED_LIBRARIES "m")
endif()

# include checks
if(APPLE OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(HAVE_DLFCN_H 1)
  set(HAVE_ERRNO_H 1)
  set(HAVE_FCNTL_H 1)
  if(APPLE)
    set(HAVE_LINK_H 0)
  else()
    set(HAVE_LINK_H 1)
  endif()
  set(HAVE_SIGNAL_H 1)
  set(HAVE_SYS_IOCTL_H 1)
  set(HAVE_SYS_RESOURCE_H 1)
  set(HAVE_SYS_STAT_H 1)
  set(HAVE_SYS_TIME_H 1)
  set(HAVE_SYS_TYPES_H 1)
  set(HAVE_SYSEXITS_H 1)
  set(HAVE_UNISTD_H 1)
else()
  set(HAVE_DLFCN_H 0)
  set(HAVE_ERRNO_H 0)
  set(HAVE_FCNTL_H 0)
  set(HAVE_LINK_H 0)
  set(HAVE_SIGNAL_H 0)
  set(HAVE_SYS_IOCTL_H 0)
  set(HAVE_SYS_RESOURCE_H 0)
  set(HAVE_SYS_STAT_H 0)
  set(HAVE_SYS_TIME_H 0)
  set(HAVE_SYS_TYPES_H 0)
  set(HAVE_SYSEXITS_H 0)
  set(HAVE_UNISTD_H 0)
endif()
set(HAVE_MALLOC_MALLOC_H 0)
if( LLVM_ENABLE_THREADS AND NOT PURE_WINDOWS )
  if(APPLE OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(HAVE_PTHREAD_H 1)
  else()
    set(HAVE_PTHREAD_H 0)
  endif()
else()
  set(HAVE_PTHREAD_H 0)
endif()
set(HAVE_SYS_MMAN_H 0)
set(HAVE_SYS_PARAM_H 0)
set(HAVE_TERMIOS_H 0)
set(HAVE_VALGRIND_VALGRIND_H 0)
if(APPLE OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(HAVE_FENV_H 1)
  set(HAVE_DECL_FE_ALL_EXCEPT 1)
  set(HAVE_DECL_FE_INEXACT 1)
else()
  set(HAVE_FENV_H 0)
  set(HAVE_DECL_FE_ALL_EXCEPT 0)
  set(HAVE_DECL_FE_INEXACT 0)
endif()
set(HAVE_BUILTIN_THREAD_POINTER 0)

if(APPLE AND (LLVM_ENABLE_BACKTRACES OR LLVM_ENABLE_CRASH_OVERRIDES))
  check_include_file(mach/mach.h HAVE_MACH_MACH_H)
  if(LLVM_ENABLE_BACKTRACES)
    check_include_file(CrashReporterClient.h HAVE_CRASHREPORTERCLIENT_H)
  else()
    set(HAVE_CRASHREPORTERCLIENT_H 0)
  endif()
else()
  set(HAVE_MACH_MACH_H 0)
  set(HAVE_CRASHREPORTERCLIENT_H 0)
endif()
if(APPLE AND LLVM_ENABLE_BACKTRACES)
  check_c_source_compiles("
     static const char *__crashreporter_info__ = 0;
     asm(\".desc ___crashreporter_info__, 0x10\");
     int main(void) { return 0; }"
    HAVE_CRASHREPORTER_INFO)
else()
  set(HAVE_CRASHREPORTER_INFO 0)
endif()

if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
  set(HAVE_LINUX_MAGIC_H 0)
  set(HAVE_LINUX_NFS_FS_H 0)
  set(HAVE_LINUX_SMB_H 0)
endif()

# library checks
if( LLVM_ENABLE_THREADS AND NOT PURE_WINDOWS )
  if(APPLE OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(HAVE_LIBPTHREAD 1)
    set(PTHREAD_IN_LIBC 0)
    set(HAVE_PTHREAD_RWLOCK_INIT 1)
    set(HAVE_PTHREAD_MUTEX_LOCK 1)
  else()
    set(HAVE_LIBPTHREAD 0)
    set(PTHREAD_IN_LIBC 0)
    set(HAVE_PTHREAD_RWLOCK_INIT 0)
    set(HAVE_PTHREAD_MUTEX_LOCK 0)
  endif()
else()
  set(HAVE_LIBPTHREAD 0)
  set(PTHREAD_IN_LIBC 0)
  set(HAVE_PTHREAD_RWLOCK_INIT 0)
  set(HAVE_PTHREAD_MUTEX_LOCK 0)
endif()
if( NOT PURE_WINDOWS )
  if(APPLE)
    set(HAVE_LIBDL 0)
    set(HAVE_LIBRT 0)
  elseif("${CMAKE_SYSTEM_NAME}" STREQUAL "Linux")
    set(HAVE_LIBDL 1)
  else()
    set(HAVE_LIBDL 0)
  endif()
  set(HAVE_LIBRT 0)
endif()

if(HAVE_LIBPTHREAD)
  # We want to find pthreads library and at the moment we do want to
  # have it reported as '-l<lib>' instead of '-pthread'.
  # TODO: switch to -pthread once the rest of the build system can deal with it.
  set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
  set(THREADS_HAVE_PTHREAD_ARG Off)
  find_package(Threads REQUIRED)
  set(LLVM_PTHREAD_LIB ${CMAKE_THREAD_LIBS_INIT})
endif()

set(HAVE_ZLIB 0)
set(LLVM_ENABLE_ZLIB 0)

set(zstd_FOUND 0)
set(LLVM_ENABLE_ZSTD 0)
set(HAVE_LIBEDIT 0)
set(LLVM_ENABLE_TERMINFO 0)

# function checks
if(APPLE)
  set(HAVE_DECL_ARC4RANDOM 1)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(HAVE_DECL_ARC4RANDOM 0)
else()
  check_symbol_exists(arc4random "stdlib.h" HAVE_DECL_ARC4RANDOM)
endif()
if(LLVM_ENABLE_BACKTRACES)
  find_package(Backtrace)
  set(HAVE_BACKTRACE ${Backtrace_FOUND})
  set(BACKTRACE_HEADER ${Backtrace_HEADER})
else()
  set(HAVE_BACKTRACE 0)
  set(BACKTRACE_HEADER "")
endif()

# NeverC's supported Apple hosts use fixed feature constants above, so there is
# no need to probe the compiler for availability-warning support here.
set(C_SUPPORTS_WERROR_UNGUARDED_AVAILABILITY_NEW 0)

# Determine whether we can register EH tables. NeverC's default cache disables
# unwind tables and backtraces, so skip these compiler probes in the fast path.
if(LLVM_ENABLE_UNWIND_TABLES OR LLVM_ENABLE_BACKTRACES)
  check_symbol_exists(__register_frame "${CMAKE_CURRENT_LIST_DIR}/unwind.h" HAVE_REGISTER_FRAME)
  check_symbol_exists(__deregister_frame "${CMAKE_CURRENT_LIST_DIR}/unwind.h" HAVE_DEREGISTER_FRAME)
  check_symbol_exists(__unw_add_dynamic_fde "${CMAKE_CURRENT_LIST_DIR}/unwind.h" HAVE_UNW_ADD_DYNAMIC_FDE)
else()
  set(HAVE_REGISTER_FRAME 0)
  set(HAVE_DEREGISTER_FRAME 0)
  set(HAVE_UNW_ADD_DYNAMIC_FDE 0)
endif()

if(LLVM_ENABLE_BACKTRACES)
  check_symbol_exists(_Unwind_Backtrace "unwind.h" HAVE__UNWIND_BACKTRACE)
else()
  set(HAVE__UNWIND_BACKTRACE 0)
endif()
set(HAVE_GETPAGESIZE 0)
set(HAVE_SYSCONF 0)
if(APPLE OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(HAVE_GETRUSAGE 1)
  set(HAVE_SETRLIMIT 1)
else()
  check_symbol_exists(getrusage sys/resource.h HAVE_GETRUSAGE)
  check_symbol_exists(setrlimit sys/resource.h HAVE_SETRLIMIT)
endif()
set(HAVE_ISATTY 1)
if(APPLE OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(HAVE_FUTIMENS 1)
  set(HAVE_FUTIMES 0)
else()
  check_symbol_exists(futimens sys/stat.h HAVE_FUTIMENS)
  check_symbol_exists(futimes sys/time.h HAVE_FUTIMES)
endif()
# HAVE_SIGALTSTACK is no longer consumed by the NeverC support sources.
set(HAVE_SIGALTSTACK 0)
option(LLVM_ENABLE_MALLOC_USAGE_PROBES
  "Probe optional allocator statistics APIs used only for memory-usage reporting." OFF)
if(LLVM_ENABLE_MALLOC_USAGE_PROBES)
  if(CMAKE_SYSTEM_NAME MATCHES "FreeBSD|OpenBSD|NetBSD")
    check_symbol_exists(mallctl malloc_np.h HAVE_MALLCTL)
  else()
    set(HAVE_MALLCTL 0)
  endif()
  check_symbol_exists(mallinfo malloc.h HAVE_MALLINFO)
  check_symbol_exists(mallinfo2 malloc.h HAVE_MALLINFO2)
  if(APPLE)
    check_include_file(malloc/malloc.h HAVE_MALLOC_MALLOC_H)
    if(HAVE_MALLOC_MALLOC_H)
      check_symbol_exists(malloc_zone_statistics malloc/malloc.h
                          HAVE_MALLOC_ZONE_STATISTICS)
    else()
      set(HAVE_MALLOC_ZONE_STATISTICS 0)
    endif()
  else()
    set(HAVE_MALLOC_ZONE_STATISTICS 0)
  endif()
else()
  set(HAVE_MALLCTL 0)
  set(HAVE_MALLINFO 0)
  set(HAVE_MALLINFO2 0)
  set(HAVE_MALLOC_ZONE_STATISTICS 0)
endif()
# These legacy config variables have no current consumers in the slim tree.
set(HAVE_GETRLIMIT 0)
if(APPLE OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(HAVE_POSIX_SPAWN 1)
  set(HAVE_PREAD 1)
else()
  check_symbol_exists(posix_spawn spawn.h HAVE_POSIX_SPAWN)
  check_symbol_exists(pread unistd.h HAVE_PREAD)
endif()
set(HAVE_SBRK 0)
if(APPLE OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(HAVE_STRERROR_R 1)
  set(HAVE_DECL_STRERROR_S 0)
else()
  check_symbol_exists(strerror_r string.h HAVE_STRERROR_R)
  check_symbol_exists(strerror_s string.h HAVE_DECL_STRERROR_S)
endif()
set(HAVE_SETENV 0)
if( PURE_WINDOWS )
  check_symbol_exists(_chsize_s io.h HAVE__CHSIZE_S)

  check_function_exists(_alloca HAVE__ALLOCA)
  check_function_exists(__alloca HAVE___ALLOCA)
  check_function_exists(__chkstk HAVE___CHKSTK)
  check_function_exists(__chkstk_ms HAVE___CHKSTK_MS)
  check_function_exists(___chkstk HAVE____CHKSTK)
  check_function_exists(___chkstk_ms HAVE____CHKSTK_MS)

  check_function_exists(__ashldi3 HAVE___ASHLDI3)
  check_function_exists(__ashrdi3 HAVE___ASHRDI3)
  check_function_exists(__divdi3 HAVE___DIVDI3)
  check_function_exists(__fixdfdi HAVE___FIXDFDI)
  check_function_exists(__fixsfdi HAVE___FIXSFDI)
  check_function_exists(__floatdidf HAVE___FLOATDIDF)
  check_function_exists(__lshrdi3 HAVE___LSHRDI3)
  check_function_exists(__moddi3 HAVE___MODDI3)
  check_function_exists(__udivdi3 HAVE___UDIVDI3)
  check_function_exists(__umoddi3 HAVE___UMODDI3)

  check_function_exists(__main HAVE___MAIN)
  check_function_exists(__cmpdi2 HAVE___CMPDI2)
endif()

if(APPLE)
  set(HAVE_STRUCT_STAT_ST_MTIMESPEC_TV_NSEC 1)
  set(HAVE_STRUCT_STAT_ST_MTIM_TV_NSEC 0)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(HAVE_STRUCT_STAT_ST_MTIMESPEC_TV_NSEC 0)
  set(HAVE_STRUCT_STAT_ST_MTIM_TV_NSEC 1)
else()
  CHECK_STRUCT_HAS_MEMBER("struct stat" st_mtimespec.tv_nsec
      "sys/types.h;sys/stat.h" HAVE_STRUCT_STAT_ST_MTIMESPEC_TV_NSEC)
  CHECK_STRUCT_HAS_MEMBER("struct stat" st_mtim.tv_nsec
      "sys/types.h;sys/stat.h" HAVE_STRUCT_STAT_ST_MTIM_TV_NSEC)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  check_symbol_exists(__GLIBC__ stdio.h LLVM_USING_GLIBC)
else()
  set(LLVM_USING_GLIBC 0)
endif()
if( LLVM_USING_GLIBC )
  add_compile_definitions(_GNU_SOURCE)
  list(APPEND CMAKE_REQUIRED_DEFINITIONS "-D_GNU_SOURCE")
# enable 64bit off_t on 32bit systems using glibc
  if (CMAKE_SIZEOF_VOID_P EQUAL 4)
    add_compile_definitions(_FILE_OFFSET_BITS=64)
    list(APPEND CMAKE_REQUIRED_DEFINITIONS "-D_FILE_OFFSET_BITS=64")
  endif()
endif()

# This check requires _GNU_SOURCE.
if (LLVM_ENABLE_THREADS AND NOT PURE_WINDOWS)
  if(APPLE)
    set(HAVE_PTHREAD_GETNAME_NP 0)
    set(HAVE_PTHREAD_SETNAME_NP 0)
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND LLVM_USING_GLIBC)
    set(HAVE_PTHREAD_GETNAME_NP 1)
    set(HAVE_PTHREAD_SETNAME_NP 1)
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(HAVE_PTHREAD_GETNAME_NP 0)
    set(HAVE_PTHREAD_SETNAME_NP 0)
  else()
    if (LLVM_PTHREAD_LIB)
      list(APPEND CMAKE_REQUIRED_LIBRARIES ${LLVM_PTHREAD_LIB})
    endif()
    check_symbol_exists(pthread_getname_np pthread.h HAVE_PTHREAD_GETNAME_NP)
    check_symbol_exists(pthread_setname_np pthread.h HAVE_PTHREAD_SETNAME_NP)
    if (LLVM_PTHREAD_LIB)
      list(REMOVE_ITEM CMAKE_REQUIRED_LIBRARIES ${LLVM_PTHREAD_LIB})
    endif()
  endif()
endif()

# This check requires _GNU_SOURCE.
if( HAVE_DLFCN_H )
  if(APPLE OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(HAVE_DLOPEN 1)
    set(HAVE_DLADDR 1)
  else()
    if( HAVE_LIBDL )
      list(APPEND CMAKE_REQUIRED_LIBRARIES dl)
    endif()
    check_symbol_exists(dlopen dlfcn.h HAVE_DLOPEN)
    check_symbol_exists(dladdr dlfcn.h HAVE_DLADDR)
    if( HAVE_LIBDL )
      list(REMOVE_ITEM CMAKE_REQUIRED_LIBRARIES dl)
    endif()
  endif()
endif()

# available programs checks
function(llvm_find_program name)
  string(TOUPPER ${name} NAME)
  string(REGEX REPLACE "\\." "_" NAME ${NAME})

  find_program(LLVM_PATH_${NAME} NAMES ${ARGV})
  mark_as_advanced(LLVM_PATH_${NAME})
  if(LLVM_PATH_${NAME})
    set(HAVE_${NAME} 1 CACHE INTERNAL "Is ${name} available ?")
    mark_as_advanced(HAVE_${NAME})
  else(LLVM_PATH_${NAME})
    set(HAVE_${NAME} "" CACHE INTERNAL "Is ${name} available ?")
  endif(LLVM_PATH_${NAME})
endfunction()


set(LLVM_ENABLE_FFI 0)
unset(HAVE_FFI_FFI_H CACHE)
unset(HAVE_FFI_H CACHE)
unset(HAVE_FFI_CALL CACHE)

option(LLVM_ENABLE_PROC_RUSAGE_PROBE
  "Probe proc_pid_rusage for optional timer instruction counters." OFF)
if(APPLE AND LLVM_ENABLE_PROC_RUSAGE_PROBE)
  check_symbol_exists(proc_pid_rusage "libproc.h" HAVE_PROC_PID_RUSAGE)
else()
  set(HAVE_PROC_PID_RUSAGE 0)
endif()

# Supported macOS/Linux NeverC hosts have native C++ atomics, so avoid the
# three compile/link probes in CheckAtomic.cmake on the fast path.
if((APPLE OR CMAKE_SYSTEM_NAME STREQUAL "Linux") AND
   CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|aarch64|arm64)$")
  set(HAVE_CXX_ATOMICS_WITHOUT_LIB TRUE)
  set(HAVE_CXX_ATOMICS64_WITHOUT_LIB TRUE)
  set(LLVM_HAS_ATOMICS 1)
  set(LLVM_ATOMIC_LIB)
else()
  # Define LLVM_HAS_ATOMICS if gcc or MSVC atomic builtins are supported.
  include(CheckAtomic)
endif()

if( LLVM_ENABLE_PIC )
  set(ENABLE_PIC 1)
else()
  set(ENABLE_PIC 0)
  if(APPLE)
    set(SUPPORTS_NO_PIE_FLAG 0)
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(SUPPORTS_NO_PIE_FLAG 1)
  else()
    check_cxx_compiler_flag("-fno-pie" SUPPORTS_NO_PIE_FLAG)
  endif()
  if(SUPPORTS_NO_PIE_FLAG)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fno-pie")
  endif()
endif()

if(LLVM_INCLUDE_TESTS)
  check_cxx_compiler_flag("-Wvariadic-macros" SUPPORTS_VARIADIC_MACROS_FLAG)
  check_cxx_compiler_flag("-Wgnu-zero-variadic-macro-arguments"
                          SUPPORTS_GNU_ZERO_VARIADIC_MACRO_ARGUMENTS_FLAG)
endif()

set(USE_NO_MAYBE_UNINITIALIZED 0)
set(USE_NO_UNINITIALIZED 0)

# Disable gcc's potentially uninitialized use analysis as it presents lots of
# false positives.
if ((NOT DEFINED LLVM_ENABLE_WARNINGS OR LLVM_ENABLE_WARNINGS) AND
    CMAKE_COMPILER_IS_GNUCXX)
  # Disable all -Wuninitialized warning for old GCC versions.
  if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 12.0)
    check_cxx_compiler_flag("-Wuninitialized" HAS_UNINITIALIZED)
    set(USE_NO_UNINITIALIZED ${HAS_UNINITIALIZED})
  else()
    check_cxx_compiler_flag("-Wmaybe-uninitialized" HAS_MAYBE_UNINITIALIZED)
    set(USE_NO_MAYBE_UNINITIALIZED ${HAS_MAYBE_UNINITIALIZED})
  endif()
endif()

if(LLVM_INCLUDE_TESTS)
  include(GetErrcMessages)
  get_errc_messages(LLVM_LIT_ERRC_MESSAGES)
endif()

# By default, we target the host, but this can be overridden at CMake
# invocation time.
include(GetHostTriple)
get_host_triple(LLVM_INFERRED_HOST_TRIPLE)

set(LLVM_HOST_TRIPLE "${LLVM_INFERRED_HOST_TRIPLE}" CACHE STRING
    "Host on which LLVM binaries will run")
message(STATUS "LLVM host triple: ${LLVM_HOST_TRIPLE}")

# Determine the native architecture.
string(TOLOWER "${LLVM_TARGET_ARCH}" LLVM_NATIVE_ARCH)
if( LLVM_NATIVE_ARCH STREQUAL "host" )
  string(REGEX MATCH "^[^-]*" LLVM_NATIVE_ARCH ${LLVM_HOST_TRIPLE})
endif ()

if (LLVM_NATIVE_ARCH MATCHES "i[2-6]86")
  set(LLVM_NATIVE_ARCH X86)
elseif (LLVM_NATIVE_ARCH STREQUAL "x86")
  set(LLVM_NATIVE_ARCH X86)
elseif (LLVM_NATIVE_ARCH STREQUAL "amd64")
  set(LLVM_NATIVE_ARCH X86)
elseif (LLVM_NATIVE_ARCH STREQUAL "x86_64")
  set(LLVM_NATIVE_ARCH X86)
elseif (LLVM_NATIVE_ARCH MATCHES "aarch64")
  set(LLVM_NATIVE_ARCH AArch64)
elseif (LLVM_NATIVE_ARCH MATCHES "arm64")
  set(LLVM_NATIVE_ARCH AArch64)
else ()
  message(FATAL_ERROR
    "NeverC host architecture ${LLVM_NATIVE_ARCH} is unsupported; "
    "supported host architectures are x86, x86_64, arm64, and aarch64.")
endif ()

# If build targets includes "host" or "Native", then replace with native architecture.
foreach (NATIVE_KEYWORD host Native)
  list(FIND LLVM_TARGETS_TO_BUILD ${NATIVE_KEYWORD} idx)
  if( NOT idx LESS 0 )
    list(REMOVE_AT LLVM_TARGETS_TO_BUILD ${idx})
    list(APPEND LLVM_TARGETS_TO_BUILD ${LLVM_NATIVE_ARCH})
    list(REMOVE_DUPLICATES LLVM_TARGETS_TO_BUILD)
  endif()
endforeach()

if (NOT ${LLVM_NATIVE_ARCH} IN_LIST LLVM_TARGETS_TO_BUILD)
  message(STATUS
    "Native target ${LLVM_NATIVE_ARCH} is not selected")
else ()
  message(STATUS "Native target architecture is ${LLVM_NATIVE_ARCH}")
  set(LLVM_NATIVE_TARGET LLVMInitialize${LLVM_NATIVE_ARCH}Target)
  set(LLVM_NATIVE_TARGETINFO LLVMInitialize${LLVM_NATIVE_ARCH}TargetInfo)
  set(LLVM_NATIVE_TARGETMC LLVMInitialize${LLVM_NATIVE_ARCH}TargetMC)
  set(LLVM_NATIVE_ASMPRINTER LLVMInitialize${LLVM_NATIVE_ARCH}AsmPrinter)

  # We don't have an ASM parser for all architectures yet.
  if (EXISTS ${PROJECT_SOURCE_DIR}/lib/Target/${LLVM_NATIVE_ARCH}/AsmParser/CMakeLists.txt)
    set(LLVM_NATIVE_ASMPARSER LLVMInitialize${LLVM_NATIVE_ARCH}AsmParser)
  endif ()

  # We don't have an disassembler for all architectures yet.
  if (EXISTS ${PROJECT_SOURCE_DIR}/lib/Target/${LLVM_NATIVE_ARCH}/Disassembler/CMakeLists.txt)
    set(LLVM_NATIVE_DISASSEMBLER LLVMInitialize${LLVM_NATIVE_ARCH}Disassembler)
  endif ()
endif ()

if( MSVC )
  set(SHLIBEXT ".lib")
  set(stricmp "_stricmp")
  set(strdup "_strdup")

  # Allow setting clang-cl's /winsysroot flag.
  set(LLVM_WINSYSROOT "" CACHE STRING
    "If set, argument to clang-cl's /winsysroot")

  if (LLVM_WINSYSROOT)
    set(MSVC_DIA_SDK_DIR "${LLVM_WINSYSROOT}/DIA SDK" CACHE PATH
        "Path to the DIA SDK")
  else()
    set(MSVC_DIA_SDK_DIR "$ENV{VSINSTALLDIR}DIA SDK" CACHE PATH
        "Path to the DIA SDK")
  endif()

  # See if the DIA SDK is available and usable.
  # Due to a bug in MSVC 2013's installation software, it is possible
  # for MSVC 2013 to write the DIA SDK into the Visual Studio 2012
  # install directory.  If this happens, the installation is corrupt
  # and there's nothing we can do.  It happens with enough frequency
  # though that we should handle it.  We do so by simply checking that
  # the DIA SDK folder exists.  Should this happen you will need to
  # uninstall VS 2012 and then re-install VS 2013.
  if (IS_DIRECTORY "${MSVC_DIA_SDK_DIR}")
    set(HAVE_DIA_SDK 1)
  else()
    set(HAVE_DIA_SDK 0)
  endif()

  option(LLVM_ENABLE_DIA_SDK "Use MSVC DIA SDK for debugging if available."
                             ${HAVE_DIA_SDK})

  if(LLVM_ENABLE_DIA_SDK AND NOT HAVE_DIA_SDK)
    message(FATAL_ERROR "DIA SDK not found. If you have both VS 2012 and 2013 installed, you may need to uninstall the former and re-install the latter afterwards.")
  endif()
else()
  set(LLVM_ENABLE_DIA_SDK 0)
endif( MSVC )

if( LLVM_ENABLE_THREADS )
  # Check if threading primitives aren't supported on this platform
  if( NOT HAVE_PTHREAD_H AND NOT WIN32 )
    set(LLVM_ENABLE_THREADS 0)
  endif()
endif()

if( LLVM_ENABLE_THREADS )
  message(STATUS "Threads enabled.")
else( LLVM_ENABLE_THREADS )
  message(STATUS "Threads disabled.")
endif()


set(GOLD_EXECUTABLE "" CACHE FILEPATH
    "Gold linker discovery is disabled in NeverC.")
set(LLVM_BINUTILS_INCDIR "" CACHE PATH
    "Gold plugin headers are unsupported in NeverC.")

if(CMAKE_GENERATOR MATCHES "Ninja")
  execute_process(COMMAND ${CMAKE_MAKE_PROGRAM} --version
    OUTPUT_VARIABLE NINJA_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  set(NINJA_VERSION ${NINJA_VERSION} CACHE STRING "Ninja version number" FORCE)
  message(STATUS "Ninja version: ${NINJA_VERSION}")
endif()

if(CMAKE_GENERATOR MATCHES "Ninja" AND
    NOT "${NINJA_VERSION}" VERSION_LESS "1.9.0" AND
    CMAKE_HOST_APPLE AND CMAKE_HOST_SYSTEM_VERSION VERSION_GREATER "15.6.0")
  set(LLVM_TOUCH_STATIC_LIBRARIES ON)
endif()

if(CMAKE_HOST_APPLE AND APPLE)
  set(LD64_EXECUTABLE "" CACHE PATH "ld64 discovery is disabled in NeverC.")
endif()


function(find_python_module module)
  string(REPLACE "." "_" module_name ${module})
  string(TOUPPER ${module_name} module_upper)
  set(FOUND_VAR PY_${module_upper}_FOUND)
  if (DEFINED ${FOUND_VAR})
    return()
  endif()

  execute_process(COMMAND "${Python3_EXECUTABLE}" "-c" "import ${module}"
    RESULT_VARIABLE status
    ERROR_QUIET)

  if(status)
    set(${FOUND_VAR} OFF CACHE BOOL "Failed to find python module '${module}'")
    message(STATUS "Could NOT find Python module ${module}")
  else()
  set(${FOUND_VAR} ON CACHE BOOL "Found python module '${module}'")
    message(STATUS "Found Python module ${module}")
  endif()
endfunction()

if(LLVM_INCLUDE_TOOLS OR LLVM_INCLUDE_DOCS)
  set(PYTHON_MODULES
    pygments
    # Some systems still don't have pygments.lexers.c_cpp which was introduced in
    # version 2.0 in 2014...
    pygments.lexers.c_cpp
    yaml
    )
  foreach(module ${PYTHON_MODULES})
    find_python_module(${module})
  endforeach()

  if(PY_PYGMENTS_FOUND AND PY_PYGMENTS_LEXERS_C_CPP_FOUND AND PY_YAML_FOUND)
    set(LLVM_HAVE_OPT_VIEWER_MODULES 1)
  else()
    set(LLVM_HAVE_OPT_VIEWER_MODULES 0)
  endif()
else()
  set(LLVM_HAVE_OPT_VIEWER_MODULES 0)
endif()

function(llvm_get_host_prefixes_and_suffixes)
  # Not all platform files will set these variables (relying on them being
  # implicitly empty if they're unset), so unset the variables before including
  # the platform file, to prevent any values from the target system leaking.
  unset(CMAKE_STATIC_LIBRARY_PREFIX)
  unset(CMAKE_STATIC_LIBRARY_SUFFIX)
  unset(CMAKE_SHARED_LIBRARY_PREFIX)
  unset(CMAKE_SHARED_LIBRARY_SUFFIX)
  unset(CMAKE_IMPORT_LIBRARY_PREFIX)
  unset(CMAKE_IMPORT_LIBRARY_SUFFIX)
  unset(CMAKE_EXECUTABLE_SUFFIX)
  unset(CMAKE_LINK_LIBRARY_SUFFIX)
  include(Platform/${CMAKE_HOST_SYSTEM_NAME} OPTIONAL RESULT_VARIABLE _includedFile)
  if (_includedFile)
    set(LLVM_HOST_STATIC_LIBRARY_PREFIX ${CMAKE_STATIC_LIBRARY_PREFIX} PARENT_SCOPE)
    set(LLVM_HOST_STATIC_LIBRARY_SUFFIX ${CMAKE_STATIC_LIBRARY_SUFFIX} PARENT_SCOPE)
    set(LLVM_HOST_SHARED_LIBRARY_PREFIX ${CMAKE_SHARED_LIBRARY_PREFIX} PARENT_SCOPE)
    set(LLVM_HOST_SHARED_LIBRARY_SUFFIX ${CMAKE_SHARED_LIBRARY_SUFFIX} PARENT_SCOPE)
    set(LLVM_HOST_IMPORT_LIBRARY_PREFIX ${CMAKE_IMPORT_LIBRARY_PREFIX} PARENT_SCOPE)
    set(LLVM_HOST_IMPORT_LIBRARY_SUFFIX ${CMAKE_IMPORT_LIBRARY_SUFFIX} PARENT_SCOPE)
    set(LLVM_HOST_EXECUTABLE_SUFFIX ${CMAKE_EXECUTABLE_SUFFIX} PARENT_SCOPE)
    set(LLVM_HOST_LINK_LIBRARY_SUFFIX ${CMAKE_LINK_LIBRARY_SUFFIX} PARENT_SCOPE)
  endif()
endfunction()

llvm_get_host_prefixes_and_suffixes()
