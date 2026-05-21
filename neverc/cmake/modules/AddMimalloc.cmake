# AddMimalloc.cmake — fetch & statically link mimalloc with MI_OVERRIDE=ON.
# Replaces malloc/free + operator new/delete across the whole process.

if(_NEVERC_ADD_MIMALLOC_INCLUDED)
  return()
endif()
set(_NEVERC_ADD_MIMALLOC_INCLUDED TRUE)

include(FetchContent)

# Auto-detect the latest mimalloc release tag from GitHub unless the
# user explicitly sets NEVERC_MIMALLOC_GIT_TAG on the command line.
if(NOT DEFINED NEVERC_MIMALLOC_GIT_TAG OR NEVERC_MIMALLOC_GIT_TAG STREQUAL "")
  file(DOWNLOAD
    "https://api.github.com/repos/microsoft/mimalloc/releases/latest"
    "${CMAKE_BINARY_DIR}/_mimalloc_latest.json"
    STATUS _mi_dl_status
    TIMEOUT 10)
  list(GET _mi_dl_status 0 _mi_dl_rc)
  if(_mi_dl_rc EQUAL 0)
    file(READ "${CMAKE_BINARY_DIR}/_mimalloc_latest.json" _mi_json)
    string(JSON _mi_latest_tag GET "${_mi_json}" "tag_name")
    set(NEVERC_MIMALLOC_GIT_TAG "${_mi_latest_tag}" CACHE STRING
        "Mimalloc git tag (auto-detected from GitHub)." FORCE)
    message(STATUS "mimalloc: auto-detected latest release ${_mi_latest_tag}")
  else()
    set(NEVERC_MIMALLOC_GIT_TAG "v3.3.2" CACHE STRING
        "Mimalloc git tag (fallback, GitHub unreachable)." FORCE)
    message(STATUS "mimalloc: GitHub unreachable, falling back to v3.3.2")
  endif()
  mark_as_advanced(NEVERC_MIMALLOC_GIT_TAG)
endif()

function(neverc_fetch_mimalloc)
  set(MI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
  set(MI_BUILD_STATIC ON  CACHE BOOL "" FORCE)
  set(MI_BUILD_OBJECT OFF CACHE BOOL "" FORCE)
  set(MI_BUILD_TESTS  OFF CACHE BOOL "" FORCE)
  # MI_OVERRIDE=ON: register malloc_zone_t on macOS / interpose on Linux
  # so that free() from libc++.dylib routes through mimalloc correctly.
  set(MI_OVERRIDE     ON  CACHE BOOL "" FORCE)
  set(MI_INSTALL_TOPLEVEL OFF CACHE BOOL "" FORCE)
  set(MI_NO_PADDING   OFF CACHE BOOL "" FORCE)
  set(MI_SKIP_COLLECT_ON_EXIT ON CACHE BOOL "" FORCE)

  FetchContent_Declare(mimalloc
    GIT_REPOSITORY https://github.com/microsoft/mimalloc.git
    GIT_TAG        ${NEVERC_MIMALLOC_GIT_TAG}
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   FALSE
  )
  # mimalloc's upstream CMake always emits install() rules; we only need
  # the static library and don't want mimalloc headers/cmake/pkgconfig in
  # the parent project's install tree.
  #
  # CMAKE_SKIP_INSTALL_RULES suppresses generation of the subdirectory's
  # cmake_install.cmake while the parent still emits an include() of it,
  # breaking `cmake --install`. Shadow install() with a no-op macro
  # instead. The macro is gated on a cache flag so the parent project's
  # own install() calls still reach the builtin (accessible via _install
  # after the macro override) once the flag is cleared.
  set(_NEVERC_SUPPRESS_INSTALL ON CACHE INTERNAL "" FORCE)
  macro(install)
    if(NOT _NEVERC_SUPPRESS_INSTALL)
      _install(${ARGV})
    endif()
  endmacro()
  FetchContent_MakeAvailable(mimalloc)
  set(_NEVERC_SUPPRESS_INSTALL OFF CACHE INTERNAL "" FORCE)

  if(NOT TARGET mimalloc-static)
    message(FATAL_ERROR
      "AddMimalloc: 'mimalloc-static' target was not created.")
  endif()

  if(NOT MSVC)
    target_compile_options(mimalloc-static PRIVATE -w)
  endif()
  set_target_properties(mimalloc-static PROPERTIES FOLDER "Third-Party")
endfunction()

# neverc_link_mimalloc(<target>)
function(neverc_link_mimalloc target)
  if(NOT TARGET mimalloc-static)
    message(FATAL_ERROR
      "neverc_link_mimalloc: call neverc_fetch_mimalloc() first.")
  endif()

  target_include_directories(${target} PRIVATE
    "${mimalloc_SOURCE_DIR}/include")
  target_compile_definitions(${target} PRIVATE NEVERC_USE_MIMALLOC=1)
  target_link_libraries(${target} PRIVATE mimalloc-static)

  if(APPLE)
    target_link_options(${target} PRIVATE
      "LINKER:-force_load,$<TARGET_FILE:mimalloc-static>")
  elseif(MSVC)
    target_link_options(${target} PRIVATE
      "/WHOLEARCHIVE:$<TARGET_FILE:mimalloc-static>")
  else()
    target_link_options(${target} PRIVATE
      "LINKER:--whole-archive"
      "$<TARGET_FILE:mimalloc-static>"
      "LINKER:--no-whole-archive")
  endif()
endfunction()
