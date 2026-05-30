# ============================================================================
# NeverC Plugin Examples
# ============================================================================
#
# Works in TWO modes:
#
#   1. In-tree (as part of the neverc build):
#      Parent sets NEVERC_INCLUDE_DIR before add_subdirectory().
#
#   2. Standalone (shipped with the Plugin SDK):
#      cmake -S /path/to/pluginsdk/examples -B build
#      cmake --build build
#
#      Or compile directly (no CMake required):
#        cc -shared -o MyPlugin.dll MyPlugin.c -I/path/to/pluginsdk/include
#
# Usage:
#   neverc -fplugin-pass=./MyPlugin.dll input.c -o output.obj
#

cmake_minimum_required(VERSION 3.20)
project(NevercExamplePlugin C)

# --- Locate the SDK header ---------------------------------------------------
if(NOT DEFINED NEVERC_INCLUDE_DIR)
  # Standalone mode: look for the header relative to this file.
  # Layout in the distributed SDK:
  #   pluginsdk/include/neverc/Plugin/NevercPluginAPI.h
  #   pluginsdk/examples/CMakeLists.txt  <-- this file
  # Layout in the source tree:
  #   pluginsdk/examples/CMakeLists.txt  <-- this file
  #   neverc/include/neverc/Plugin/NevercPluginAPI.h
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../include/neverc/Plugin/NevercPluginAPI.h")
    set(NEVERC_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../include")
  elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include/neverc/Plugin/NevercPluginAPI.h")
    set(NEVERC_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../neverc/include")
  else()
    message(FATAL_ERROR
      "NevercPluginAPI.h not found.\n"
      "Set -DNEVERC_INCLUDE_DIR=/path/to/include  (parent of neverc/Plugin/)")
  endif()
endif()

# --- Helper macro: add_neverc_plugin(name source.c [source2.c ...]) ----------
macro(add_neverc_plugin _name)
  add_library(${_name} SHARED ${ARGN})
  target_include_directories(${_name} PRIVATE "${NEVERC_INCLUDE_DIR}")
  set_target_properties(${_name} PROPERTIES
    PREFIX ""
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
  )
  if(WIN32)
    set_target_properties(${_name} PROPERTIES SUFFIX ".dll")
  elseif(APPLE)
    set_target_properties(${_name} PROPERTIES SUFFIX ".dylib")
  else()
    set_target_properties(${_name} PROPERTIES SUFFIX ".so")
  endif()
endmacro()

# --- Example plugins ---------------------------------------------------------
add_neverc_plugin(ExamplePlugin   ExamplePlugin.c)
add_neverc_plugin(CrtShimPlugin   CrtShimPlugin.c)
add_neverc_plugin(BenchPlugin     BenchPlugin.c)

# --- Add your own plugins below ----------------------------------------------
# add_neverc_plugin(MyPlugin MyPlugin.c)

# --- Optional: no-CRT lockdown variant --------------------------------------
# Skipped on MSVC where /NODEFAULTLIB requires a different incantation.
# Enable with -DNEVERC_BUILD_NO_CRT=ON.
option(NEVERC_BUILD_NO_CRT
  "Build CrtShimPlugin with -nodefaultlibs to prove zero CRT dependency"
  OFF)

if(NEVERC_BUILD_NO_CRT AND NOT MSVC)
  add_library(CrtShimPluginNoCRT SHARED CrtShimPlugin.c)
  target_include_directories(CrtShimPluginNoCRT PRIVATE "${NEVERC_INCLUDE_DIR}")
  set_target_properties(CrtShimPluginNoCRT PROPERTIES
    PREFIX ""
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
  )
  target_compile_options(CrtShimPluginNoCRT PRIVATE
    -fno-stack-protector
    -fno-builtin
    -fvisibility=hidden
    -ffreestanding
  )
  target_link_options(CrtShimPluginNoCRT PRIVATE -nodefaultlibs)
  if(APPLE)
    target_link_libraries(CrtShimPluginNoCRT PRIVATE System)
    target_link_options(CrtShimPluginNoCRT PRIVATE "-Wl,-undefined,error")
    set_target_properties(CrtShimPluginNoCRT PROPERTIES SUFFIX ".dylib")
  else()
    target_link_options(CrtShimPluginNoCRT PRIVATE "-Wl,--no-undefined")
    set_target_properties(CrtShimPluginNoCRT PROPERTIES SUFFIX ".so")
  endif()
endif()
