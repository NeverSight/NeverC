# Fetch reproducible, static compression dependencies for NeverC toolchain
# builds. System LLVM builds can leave LLVM_USE_BUNDLED_{ZLIB,ZSTD} disabled
# and continue through the normal find_package paths in config-ix.cmake.

include_guard(GLOBAL)
include(FetchContent)

set(LLVM_BUNDLED_ZLIB_URL
    "https://zlib.net/zlib-1.3.2.tar.gz"
    CACHE STRING "Source archive used for the bundled zlib build")
set(LLVM_BUNDLED_ZLIB_SHA256
    "bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16"
    CACHE STRING "SHA-256 of LLVM_BUNDLED_ZLIB_URL")
set(LLVM_BUNDLED_ZSTD_URL
    "https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz"
    CACHE STRING "Source archive used for the bundled zstd build")
set(LLVM_BUNDLED_ZSTD_SHA256
    "eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3"
    CACHE STRING "SHA-256 of LLVM_BUNDLED_ZSTD_URL")
mark_as_advanced(
  LLVM_BUNDLED_ZLIB_URL
  LLVM_BUNDLED_ZLIB_SHA256
  LLVM_BUNDLED_ZSTD_URL
  LLVM_BUNDLED_ZSTD_SHA256)

function(llvm_fetch_compression_dependencies)
  # DOWNLOAD_EXTRACT_TIMESTAMP was added with CMP0135 in CMake 3.24, while
  # LLVM still supports CMake 3.20. On newer CMake versions, opt in explicitly
  # so changing a pinned archive reliably rebuilds its extracted sources.
  set(_llvm_compression_archive_options)
  if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.24)
    list(APPEND _llvm_compression_archive_options
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  endif()

  if(LLVM_USE_BUNDLED_ZLIB AND LLVM_ENABLE_ZLIB)
    set(ZLIB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(ZLIB_BUILD_STATIC ON CACHE BOOL "" FORCE)
    set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(ZLIB_BUILD_MINIZIP OFF CACHE BOOL "" FORCE)
    # Install the static library and public headers with the toolchain: LLVM's
    # installed Compression.h exposes this dependency to downstream consumers.
    set(ZLIB_INSTALL ON CACHE BOOL "" FORCE)

    FetchContent_Declare(zlib
      URL "${LLVM_BUNDLED_ZLIB_URL}"
      URL_HASH "SHA256=${LLVM_BUNDLED_ZLIB_SHA256}"
      ${_llvm_compression_archive_options})
    FetchContent_MakeAvailable(zlib)

    if(NOT TARGET ZLIB::ZLIBSTATIC)
      message(FATAL_ERROR
        "Bundled zlib did not create the required ZLIB::ZLIBSTATIC target")
    endif()
    # Preserve LLVM's conventional dependency name. The installed FindZLIB
    # module recreates ZLIB::ZLIB and will resolve it to the installed static
    # archive because no shared zlib is bundled.
    if(NOT TARGET ZLIB::ZLIB)
      add_library(ZLIB::ZLIB INTERFACE IMPORTED GLOBAL)
      set_property(TARGET ZLIB::ZLIB PROPERTY
        INTERFACE_LINK_LIBRARIES ZLIB::ZLIBSTATIC)
    endif()
    if(TARGET zlibstatic)
      set_target_properties(zlibstatic PROPERTIES FOLDER "Third-Party")
      if(MSVC)
        target_compile_options(zlibstatic PRIVATE /w)
      else()
        target_compile_options(zlibstatic PRIVATE -w)
      endif()
    endif()
  endif()

  if(LLVM_USE_BUNDLED_ZSTD AND LLVM_ENABLE_ZSTD)
    set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
    set(ZSTD_MULTITHREAD_SUPPORT ON CACHE BOOL "" FORCE)

    FetchContent_Declare(zstd
      URL "${LLVM_BUNDLED_ZSTD_URL}"
      URL_HASH "SHA256=${LLVM_BUNDLED_ZSTD_SHA256}"
      ${_llvm_compression_archive_options}
      SOURCE_SUBDIR build/cmake)
    FetchContent_MakeAvailable(zstd)

    if(NOT TARGET libzstd_static)
      message(FATAL_ERROR
        "Bundled zstd did not create the required libzstd_static target")
    endif()
    if(NOT TARGET zstd::libzstd_static)
      add_library(zstd::libzstd_static ALIAS libzstd_static)
    endif()
    set_target_properties(libzstd_static PROPERTIES FOLDER "Third-Party")
    if(MSVC)
      target_compile_options(libzstd_static PRIVATE /w)
    else()
      target_compile_options(libzstd_static PRIVATE -w)
    endif()
  endif()
endfunction()
