# pluginsdk/ — Out-of-tree plugin SDK (header + examples).
#
# Copied into the build tree so the NeverC binary can discover them
# relative to its own install location:
#   <InstalledDir>/../pluginsdk/include/neverc/Plugin/NevercPluginAPI.h
#   <InstalledDir>/../pluginsdk/examples/
#
# Uses stamp files to avoid re-copying on every build.
# Delete a stamp to force a refresh:
#   rm <build>/pluginsdk/*.stamp

set(PLUGINSDK_SRC  "${CMAKE_CURRENT_SOURCE_DIR}")
set(PLUGINSDK_DST  "${LLVM_LIBRARY_OUTPUT_INTDIR}/../pluginsdk")

# Source locations of the artefacts we aggregate into the SDK.
set(PLUGIN_HEADER
    "${PLUGINSDK_SRC}/../neverc/include/neverc/Plugin/NevercPluginAPI.h")
set(PLUGIN_EXAMPLES_DIR
    "${PLUGINSDK_SRC}/examples")

# ---------------------------------------------------------------------------
# Build-tree copy
# ---------------------------------------------------------------------------
set(_stamp "${CMAKE_CURRENT_BINARY_DIR}/pluginsdk.stamp")

add_custom_command(
  OUTPUT "${_stamp}"
  # include/neverc/Plugin/
  COMMAND "${CMAKE_COMMAND}" -E make_directory
      "${PLUGINSDK_DST}/include/neverc/Plugin"
  COMMAND "${CMAKE_COMMAND}" -E copy
      "${PLUGIN_HEADER}"
      "${PLUGINSDK_DST}/include/neverc/Plugin/NevercPluginAPI.h"
  # examples/
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${PLUGINSDK_DST}/examples"
  COMMAND "${CMAKE_COMMAND}" -E copy
      "${PLUGIN_EXAMPLES_DIR}/CMakeLists.txt"
      "${PLUGINSDK_DST}/examples/CMakeLists.txt"
  COMMAND "${CMAKE_COMMAND}" -E copy
      "${PLUGIN_EXAMPLES_DIR}/ExamplePlugin.c"
      "${PLUGINSDK_DST}/examples/ExamplePlugin.c"
  COMMAND "${CMAKE_COMMAND}" -E copy
      "${PLUGIN_EXAMPLES_DIR}/CrtShimPlugin.c"
      "${PLUGINSDK_DST}/examples/CrtShimPlugin.c"
  COMMAND "${CMAKE_COMMAND}" -E copy
      "${PLUGIN_EXAMPLES_DIR}/BenchPlugin.c"
      "${PLUGINSDK_DST}/examples/BenchPlugin.c"
  COMMAND "${CMAKE_COMMAND}" -E touch "${_stamp}"
  DEPENDS
      "${PLUGIN_HEADER}"
      "${PLUGIN_EXAMPLES_DIR}/CMakeLists.txt"
      "${PLUGIN_EXAMPLES_DIR}/ExamplePlugin.c"
      "${PLUGIN_EXAMPLES_DIR}/CrtShimPlugin.c"
      "${PLUGIN_EXAMPLES_DIR}/BenchPlugin.c"
  COMMENT "Copying pluginsdk to build tree..."
)

add_custom_target(neverc-pluginsdk ALL DEPENDS "${_stamp}")

# ---------------------------------------------------------------------------
# CMake install
# ---------------------------------------------------------------------------
install(FILES "${PLUGIN_HEADER}"
        DESTINATION "pluginsdk/include/neverc/Plugin"
        COMPONENT neverc-pluginsdk)

install(FILES
          "${PLUGIN_EXAMPLES_DIR}/CMakeLists.txt"
          "${PLUGIN_EXAMPLES_DIR}/ExamplePlugin.c"
          "${PLUGIN_EXAMPLES_DIR}/CrtShimPlugin.c"
          "${PLUGIN_EXAMPLES_DIR}/BenchPlugin.c"
        DESTINATION "pluginsdk/examples"
        COMPONENT neverc-pluginsdk)

if(NOT LLVM_ENABLE_IDE)
  add_llvm_install_targets(install-neverc-pluginsdk
                           DEPENDS neverc-pluginsdk
                           COMPONENT neverc-pluginsdk)
endif()
