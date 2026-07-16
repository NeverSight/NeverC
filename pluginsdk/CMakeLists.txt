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
set(PLUGIN_CORE_HEADER
    "${PLUGINSDK_SRC}/../neverc/include/neverc/Plugin/PluginCore.h")
set(PLUGIN_DRIVER_HEADER
    "${PLUGINSDK_SRC}/../neverc/include/neverc/Plugin/PluginDriver.h")
set(PLUGIN_SOURCE_HEADER
    "${PLUGINSDK_SRC}/../neverc/include/neverc/Plugin/PluginSource.h")
set(PLUGIN_PREP_HEADER
    "${PLUGINSDK_SRC}/../neverc/include/neverc/Plugin/PluginPrep.h")
set(PLUGIN_AST_HEADER
    "${PLUGINSDK_SRC}/../neverc/include/neverc/Plugin/PluginAST.h")
set(PLUGIN_SEMA_HEADER
    "${PLUGINSDK_SRC}/../neverc/include/neverc/Plugin/PluginSema.h")
set(PLUGIN_PHASE_SCHEMA_HEADER
    "${PLUGINSDK_SRC}/../neverc/include/neverc/Plugin/PluginPhaseSchema.h")
set(PLUGIN_PHASE_SCHEMA_INC
    "${PLUGINSDK_SRC}/../neverc/include/neverc/Plugin/Schema/PluginPhaseSchema.inc")
set(PLUGIN_PREP_SCHEMA_JSON
    "${PLUGINSDK_SRC}/../neverc/include/neverc/Plugin/Schema/PrepSchema.json")
set(PLUGIN_PREP_SCHEMA_INC
    "${PLUGINSDK_SRC}/../neverc/include/neverc/Plugin/Schema/PluginPrepSchema.inc")
set(PLUGIN_AST_SCHEMA_JSON
    "${PLUGINSDK_SRC}/../neverc/include/neverc/Plugin/Schema/ASTSchema.json")
set(PLUGIN_AST_SCHEMA_INC
    "${PLUGINSDK_SRC}/../neverc/include/neverc/Plugin/Schema/PluginASTSchema.inc")

# All files shipped in the examples/ directory (add new plugins here).
set(EXAMPLE_FILES
    Makefile
    ExamplePlugin.c
    CrtShimPlugin.c
    BenchPlugin.c
    CustomCallConvPlugin.c
    DriverTracePlugin.c)

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
  COMMAND "${CMAKE_COMMAND}" -E make_directory
      "${PLUGINSDK_DST}/include/neverc/Plugin/Schema"
  COMMAND "${CMAKE_COMMAND}" -E copy
      "${PLUGIN_HEADER}"
      "${PLUGINSDK_DST}/include/neverc/Plugin/NevercPluginAPI.h"
  COMMAND "${CMAKE_COMMAND}" -E copy
      "${PLUGIN_CORE_HEADER}"
      "${PLUGINSDK_DST}/include/neverc/Plugin/PluginCore.h"
  COMMAND "${CMAKE_COMMAND}" -E copy
      "${PLUGIN_DRIVER_HEADER}"
      "${PLUGINSDK_DST}/include/neverc/Plugin/PluginDriver.h"
  COMMAND "${CMAKE_COMMAND}" -E copy
      "${PLUGIN_SOURCE_HEADER}"
      "${PLUGINSDK_DST}/include/neverc/Plugin/PluginSource.h"
  COMMAND "${CMAKE_COMMAND}" -E copy
      "${PLUGIN_PREP_HEADER}"
      "${PLUGINSDK_DST}/include/neverc/Plugin/PluginPrep.h"
  COMMAND "${CMAKE_COMMAND}" -E copy
      "${PLUGIN_AST_HEADER}"
      "${PLUGINSDK_DST}/include/neverc/Plugin/PluginAST.h"
  COMMAND "${CMAKE_COMMAND}" -E copy
      "${PLUGIN_SEMA_HEADER}"
      "${PLUGINSDK_DST}/include/neverc/Plugin/PluginSema.h"
  COMMAND "${CMAKE_COMMAND}" -E copy
      "${PLUGIN_PHASE_SCHEMA_HEADER}"
      "${PLUGINSDK_DST}/include/neverc/Plugin/PluginPhaseSchema.h"
  COMMAND "${CMAKE_COMMAND}" -E copy
      "${PLUGIN_PHASE_SCHEMA_INC}"
      "${PLUGINSDK_DST}/include/neverc/Plugin/Schema/PluginPhaseSchema.inc"
  COMMAND "${CMAKE_COMMAND}" -E copy
      "${PLUGIN_PREP_SCHEMA_JSON}"
      "${PLUGINSDK_DST}/include/neverc/Plugin/Schema/PrepSchema.json"
  COMMAND "${CMAKE_COMMAND}" -E copy
      "${PLUGIN_PREP_SCHEMA_INC}"
      "${PLUGINSDK_DST}/include/neverc/Plugin/Schema/PluginPrepSchema.inc"
  COMMAND "${CMAKE_COMMAND}" -E copy
      "${PLUGIN_AST_SCHEMA_JSON}"
      "${PLUGINSDK_DST}/include/neverc/Plugin/Schema/ASTSchema.json"
  COMMAND "${CMAKE_COMMAND}" -E copy
      "${PLUGIN_AST_SCHEMA_INC}"
      "${PLUGINSDK_DST}/include/neverc/Plugin/Schema/PluginASTSchema.inc"
  COMMAND "${CMAKE_COMMAND}" -E rm -rf "${PLUGINSDK_DST}/examples"
  COMMAND "${CMAKE_COMMAND}" -E copy_directory
      "${PLUGINSDK_SRC}/examples"
      "${PLUGINSDK_DST}/examples"
  COMMAND "${CMAKE_COMMAND}" -E touch "${_stamp}"
  DEPENDS
      "${PLUGIN_HEADER}"
      "${PLUGIN_CORE_HEADER}"
      "${PLUGIN_DRIVER_HEADER}"
      "${PLUGIN_SOURCE_HEADER}"
      "${PLUGIN_PREP_HEADER}"
      "${PLUGIN_AST_HEADER}"
      "${PLUGIN_SEMA_HEADER}"
      "${PLUGIN_PHASE_SCHEMA_HEADER}"
      "${PLUGIN_PHASE_SCHEMA_INC}"
      "${PLUGIN_PREP_SCHEMA_JSON}"
      "${PLUGIN_PREP_SCHEMA_INC}"
      "${PLUGIN_AST_SCHEMA_JSON}"
      "${PLUGIN_AST_SCHEMA_INC}"
      ${_example_abs}
  COMMENT "Copying pluginsdk to build tree..."
)

add_library(neverc-plugin-example-driver-trace MODULE
            "${PLUGINSDK_SRC}/examples/DriverTracePlugin.c")
target_include_directories(neverc-plugin-example-driver-trace
                           PRIVATE "${PLUGINSDK_SRC}/../neverc/include")
target_compile_features(neverc-plugin-example-driver-trace PRIVATE c_std_11)
set_target_properties(neverc-plugin-example-driver-trace PROPERTIES
                      PREFIX ""
                      LIBRARY_OUTPUT_DIRECTORY
                        "${CMAKE_CURRENT_BINARY_DIR}/examples")

add_custom_target(neverc-pluginsdk ALL
                  DEPENDS "${_stamp}" neverc-plugin-example-driver-trace)

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
install(FILES
        "${PLUGIN_HEADER}"
        "${PLUGIN_CORE_HEADER}"
        "${PLUGIN_DRIVER_HEADER}"
        "${PLUGIN_SOURCE_HEADER}"
        "${PLUGIN_PREP_HEADER}"
        "${PLUGIN_AST_HEADER}"
        "${PLUGIN_SEMA_HEADER}"
        "${PLUGIN_PHASE_SCHEMA_HEADER}"
        DESTINATION "pluginsdk/include/neverc/Plugin"
        COMPONENT neverc-pluginsdk)
install(FILES "${PLUGIN_PHASE_SCHEMA_INC}"
              "${PLUGIN_PREP_SCHEMA_JSON}"
              "${PLUGIN_PREP_SCHEMA_INC}"
              "${PLUGIN_AST_SCHEMA_JSON}"
              "${PLUGIN_AST_SCHEMA_INC}"
        DESTINATION "pluginsdk/include/neverc/Plugin/Schema"
        COMPONENT neverc-pluginsdk)

install(FILES ${_example_abs}
        DESTINATION "pluginsdk/examples"
        COMPONENT neverc-pluginsdk)

if(NOT LLVM_ENABLE_IDE)
  add_llvm_install_targets(install-neverc-pluginsdk
                           DEPENDS neverc-pluginsdk
                           COMPONENT neverc-pluginsdk)
endif()
