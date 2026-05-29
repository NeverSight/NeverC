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

if(WIN32)
  set_target_properties(ExamplePlugin PROPERTIES SUFFIX ".dll")
elseif(APPLE)
  set_target_properties(ExamplePlugin PROPERTIES SUFFIX ".dylib")
else()
  set_target_properties(ExamplePlugin PROPERTIES SUFFIX ".so")
endif()
