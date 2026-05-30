# pluginsdk/ — Out-of-tree plugin SDK (header + examples).
#
# Copies the SDK into the build tree so the NeverC binary can discover them
# relative to its own install location:
#   <InstalledDir>/../pluginsdk/include/neverc/Plugin/NevercPluginAPI.h
#   <InstalledDir>/../pluginsdk/examples/
#
# Uses a stamp file to avoid re-copying on every build.
# Delete the stamp to force a refresh:  rm <build>/pluginsdk/*.stamp

set(PLUGINSDK_SRC "${CMAKE_CURRENT_SOURCE_DIR}")
set(PLUGINSDK_DST "${LLVM_LIBRARY_OUTPUT_INTDIR}/../pluginsdk")

set(PLUGIN_HEADER
    "${PLUGINSDK_SRC}/../neverc/include/neverc/Plugin/NevercPluginAPI.h")

# All files shipped in the examples/ directory (add new plugins here).
set(EXAMPLE_FILES
    Makefile
    ExamplePlugin.c
    CrtShimPlugin.c
    BenchPlugin.c)

list(TRANSFORM EXAMPLE_FILES
     PREPEND "${PLUGINSDK_SRC}/examples/"
     OUTPUT_VARIABLE _example_abs)

# ---------------------------------------------------------------------------
# Build-tree copy
# ---------------------------------------------------------------------------
set(_stamp "${CMAKE_CURRENT_BINARY_DIR}/pluginsdk.stamp")

add_custom_command(
  OUTPUT "${_stamp}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory
      "${PLUGINSDK_DST}/include/neverc/Plugin"
  COMMAND "${CMAKE_COMMAND}" -E copy
      "${PLUGIN_HEADER}"
      "${PLUGINSDK_DST}/include/neverc/Plugin/NevercPluginAPI.h"
  COMMAND "${CMAKE_COMMAND}" -E rm -rf "${PLUGINSDK_DST}/examples"
  COMMAND "${CMAKE_COMMAND}" -E copy_directory
      "${PLUGINSDK_SRC}/examples"
      "${PLUGINSDK_DST}/examples"
  COMMAND "${CMAKE_COMMAND}" -E touch "${_stamp}"
  DEPENDS "${PLUGIN_HEADER}" ${_example_abs}
  COMMENT "Copying pluginsdk to build tree..."
)

add_custom_target(neverc-pluginsdk ALL DEPENDS "${_stamp}")

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
install(FILES "${PLUGIN_HEADER}"
        DESTINATION "pluginsdk/include/neverc/Plugin"
        COMPONENT neverc-pluginsdk)

install(FILES ${_example_abs}
        DESTINATION "pluginsdk/examples"
        COMPONENT neverc-pluginsdk)

if(NOT LLVM_ENABLE_IDE)
  add_llvm_install_targets(install-neverc-pluginsdk
                           DEPENDS neverc-pluginsdk
                           COMPONENT neverc-pluginsdk)
endif()
