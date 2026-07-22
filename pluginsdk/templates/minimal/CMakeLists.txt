# Minimal out-of-tree NeverC plugin project.
#
# Configure against an installed SDK:
#   cmake -S . -B build -DCMAKE_PREFIX_PATH=<install-prefix>/pluginsdk/cmake
#   cmake --build build
#
# The plugin is a plain C shared module and links only the header-only
# NevercPluginSDK::headers interface target (no LLVM, no NeverC runtime).

cmake_minimum_required(VERSION 3.20)
project(neverc_minimal_plugin C)

find_package(NevercPluginSDK REQUIRED)

add_library(minimal_plugin MODULE Plugin.c)
set_target_properties(minimal_plugin PROPERTIES
  PREFIX ""
  OUTPUT_NAME "MinimalPlugin"
  C_STANDARD 11
  C_STANDARD_REQUIRED ON)
target_link_libraries(minimal_plugin PRIVATE NevercPluginSDK::headers)
