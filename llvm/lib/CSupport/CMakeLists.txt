include(GetLibraryName)

if(LLVM_ENABLE_ZLIB)
  list(APPEND imported_libs ZLIB::ZLIB)
endif()

if(LLVM_ENABLE_ZSTD)
  if(TARGET zstd::libzstd_shared AND NOT LLVM_USE_STATIC_ZSTD)
    set(zstd_target zstd::libzstd_shared)
  else()
    set(zstd_target zstd::libzstd_static)
  endif()
endif()
if(LLVM_ENABLE_ZSTD)
  list(APPEND imported_libs ${zstd_target})
endif()

if( MSVC OR MINGW )
  set(system_libs ${system_libs} psapi shell32 ole32 uuid advapi32 ws2_32)
elseif( CMAKE_HOST_UNIX )
  if( HAVE_LIBRT )
    set(system_libs ${system_libs} rt)
  endif()
  if( HAVE_LIBDL )
    set(system_libs ${system_libs} ${CMAKE_DL_LIBS})
  endif()
  if( HAVE_BACKTRACE AND NOT "${Backtrace_LIBRARIES}" STREQUAL "" )
    get_filename_component(Backtrace_LIBFILE ${Backtrace_LIBRARIES} NAME_WE)
    STRING(REGEX REPLACE "^lib" "" Backtrace_LIBFILE ${Backtrace_LIBFILE})
    set(system_libs ${system_libs} ${Backtrace_LIBFILE})
  endif()
  if( LLVM_ENABLE_TERMINFO )
    set(imported_libs ${imported_libs} Terminfo::terminfo)
  endif()
  set(system_libs ${system_libs} ${LLVM_ATOMIC_LIB})
  set(system_libs ${system_libs} ${LLVM_PTHREAD_LIB})
  if( UNIX AND NOT (BEOS OR HAIKU) )
    set(system_libs ${system_libs} m)
  endif()
  if( UNIX AND ${CMAKE_SYSTEM_NAME} MATCHES "SunOS" )
    set(system_libs ${system_libs} kstat)
  endif()
  if( FUCHSIA )
    set(system_libs ${system_libs} zircon)
  endif()
  if ( HAIKU )
    add_compile_definitions(_BSD_SOURCE)
    set(system_libs ${system_libs} bsd)
  endif()
endif( MSVC OR MINGW )

set (delayload_flags)
if (MSVC)
  set (delayload_flags delayimp -delayload:shell32.dll -delayload:ole32.dll)
endif()

if (UNIX AND ${CMAKE_SYSTEM_NAME} MATCHES "AIX")
  remove_definitions("-D_XOPEN_SOURCE=700")
endif()

add_subdirectory(BLAKE3)

set(LLVM_C_SUPPORT_SOURCES
  APFloat.c
  APInt.c
  BranchProbability.c
  BuryPointer.c
  CRC.c
  CachePruning.c
  Chrono.c
  CommandLine.c
  Compression.c
  ConvertUTF.c
  ConvertUTFWrapper.c
  DJB.c
  DynamicLibrary.c
  Errno.c
  FloatingPointMode.c
  FoldingSet.c
  FormattedStream.c
  GlobPattern.c
  GraphWriter.c
  IntEqClasses.c
  JSON.c
  LEB128.c
  LineIterator.c
  LockFileManager.c
  MD5.c
  MemAlloc.c
  Memory.c
  NativeFormatting.c
  Path.c
  Process.c
  Program.c
  RWMutex.c
  RandomNumberGenerator.c
  Regex.c
  SHA1.c
  SHA256.c
  ScaledNumber.c
  Signals.c
  SmallPtrSet.c
  SmallVector.c
  SourceMgr.c
  StringExtras.c
  StringMap.c
  StringRef.c
  TarWriter.c
  Threading.c
  Timer.c
  Unicode.c
  UnicodeCaseFold.c
  UnicodeNameToCodepoint.c
  UnicodeNameToCodepointGenerated.c
  Valgrind.c
  VersionTuple.c
  Watchdog.c
  WithColor.c
  YAMLParser.c
  raw_ostream.c
  regcomp.c
  regerror.c
  regexec.c
  regfree.c
  regstrlcpy.c
  xxhash.c
)

# Minimal C++ file for platform .inc includes with circular header deps.
# All other LLVM C++ methods are inline in headers.
add_llvm_component_library(LLVMSupport
  PARTIAL_SOURCES_INTENDED
  ${LLVM_C_SUPPORT_SOURCES}
  ${CMAKE_CURRENT_SOURCE_DIR}/../CSupportHost/support_cpp.cpp

  $<TARGET_OBJECTS:LLVMSupportBlake3>

  ADDITIONAL_HEADER_DIRS
  Unix
  Windows
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/ADT
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Support
  ${Backtrace_INCLUDE_DIRS}

  LINK_LIBS
  ${system_libs} ${imported_libs} ${delayload_flags}

  LINK_COMPONENTS
  )

target_include_directories(LLVMSupport PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
target_include_directories(LLVMSupport PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)

set(llvm_system_libs ${system_libs})

if(LLVM_ENABLE_ZLIB)
  if(CMAKE_BUILD_TYPE)
    string(TOUPPER ${CMAKE_BUILD_TYPE} build_type)
    get_property(zlib_library TARGET ZLIB::ZLIB PROPERTY LOCATION_${build_type})
  endif()
  if(NOT zlib_library)
    get_property(zlib_library TARGET ZLIB::ZLIB PROPERTY LOCATION)
  endif()
  get_library_name(${zlib_library} zlib_library)
  set(llvm_system_libs ${llvm_system_libs} "${zlib_library}")
endif()

if(LLVM_ENABLE_ZSTD)
  if(CMAKE_BUILD_TYPE)
    string(TOUPPER ${CMAKE_BUILD_TYPE} build_type)
    get_property(zstd_library TARGET ${zstd_target} PROPERTY LOCATION_${build_type})
  endif()
  if(NOT zstd_library)
    get_property(zstd_library TARGET ${zstd_target} PROPERTY LOCATION)
  endif()
  get_library_name(${zstd_library} zstd_library)
  set(llvm_system_libs ${llvm_system_libs} "${zstd_library}")
endif()

if(LLVM_ENABLE_TERMINFO)
  if(NOT terminfo_library)
    get_property(terminfo_library TARGET Terminfo::terminfo PROPERTY LOCATION)
  endif()
  get_library_name(${terminfo_library} terminfo_library)
  set(llvm_system_libs ${llvm_system_libs} "${terminfo_library}")
endif()

set_property(TARGET LLVMSupport PROPERTY LLVM_SYSTEM_LIBS "${llvm_system_libs}")

