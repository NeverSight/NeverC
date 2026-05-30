cmake_minimum_required(VERSION 3.20)

# Standalone build: this plugin only needs a C compiler and the public header.
# It does NOT link against LLVM or neverc libraries.
#
# From the neverc build tree:
#   cmake -S neverc/examples/plugin -B build-example-plugin \
#         -DNEVERC_INCLUDE_DIR=<neverc-src>/include
#   cmake --build build-example-plugin
#
# Or just: cc -shared -o ExamplePlugin.so ExamplePlugin.c -I<neverc>/include

project(NevercExamplePlugin C)

if(NOT DEFINED NEVERC_INCLUDE_DIR)
  set(NEVERC_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../include")
endif()

add_library(ExamplePlugin SHARED ExamplePlugin.c)
target_include_directories(ExamplePlugin PRIVATE "${NEVERC_INCLUDE_DIR}")

set_target_properties(ExamplePlugin PROPERTIES
  PREFIX ""
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
)

# CrtShimPlugin: minimal proof that an out-of-tree plugin can be built with
# ZERO C-runtime dependency, so it can be safely loaded by hosts compiled
# against a different (or incompatible) CRT version.  The target exists in
# two flavors:
#
#   CrtShimPlugin           -- normal build (still CRT-free at the source
#                              level, but linked the way any user might).
#   CrtShimPluginNoCRT      -- maximally locked-down build that disables
#                              compiler builtins, stack protectors and the
#                              default libraries, then hard-fails the link
#                              on any unresolved symbol.  Used by the
#                              `verify-no-crt` target to assert that the
#                              vtable abstraction is airtight.
add_library(CrtShimPlugin SHARED CrtShimPlugin.c)
target_include_directories(CrtShimPlugin PRIVATE "${NEVERC_INCLUDE_DIR}")
set_target_properties(CrtShimPlugin PROPERTIES
  PREFIX ""
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
)

# BenchPlugin: micro-benchmarks for HostAPI throughput.  Times Alloc/Free,
# Arena allocation, StrFormat, ArenaStrFormat, StrMap insertion, DynArray
# push, and raw vtable call overhead via the MonotonicNanos vtable entry.
add_library(BenchPlugin SHARED BenchPlugin.c)
target_include_directories(BenchPlugin PRIVATE "${NEVERC_INCLUDE_DIR}")
set_target_properties(BenchPlugin PROPERTIES
  PREFIX ""
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
)

if(WIN32)
  set_target_properties(ExamplePlugin CrtShimPlugin BenchPlugin
    PROPERTIES SUFFIX ".dll")
elseif(APPLE)
  set_target_properties(ExamplePlugin CrtShimPlugin BenchPlugin
    PROPERTIES SUFFIX ".dylib")
else()
  set_target_properties(ExamplePlugin CrtShimPlugin BenchPlugin
    PROPERTIES SUFFIX ".so")
endif()

# Optional: no-CRT lockdown variant.  Skipped on MSVC where /NODEFAULTLIB
# requires a different incantation and on platforms that don't support the
# combination cleanly.  Enable with -DNEVERC_BUILD_NO_CRT=ON.
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
    # macOS dyld requires every dylib to link libSystem.B.dylib for the
    # loader stubs (dyld_stub_binder, etc.) -- this is unrelated to libc
    # and applies even to assembly-only modules.  -nodefaultlibs strips
    # libc++ and compiler_rt, leaving only the loader stubs.
    target_link_libraries(CrtShimPluginNoCRT PRIVATE System)
    target_link_options(CrtShimPluginNoCRT PRIVATE "-Wl,-undefined,error")
    set_target_properties(CrtShimPluginNoCRT PROPERTIES SUFFIX ".dylib")
  else()
    target_link_options(CrtShimPluginNoCRT PRIVATE "-Wl,--no-undefined")
    set_target_properties(CrtShimPluginNoCRT PROPERTIES SUFFIX ".so")
  endif()
endif()
