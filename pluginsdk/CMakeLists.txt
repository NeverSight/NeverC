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

# SDK package version tracks the public plugin ABI major.minor (see
# NEVERC_PLUGIN_ABI_MAJOR/MINOR in PluginCore.h). Bump alongside the ABI.
set(NEVERC_PLUGIN_SDK_VERSION "1.0.0")

# The distributed aggregate is the generated, self-contained single header
# (utils/plugin-api/gen-single-header.py), so the SDK ships one pure-C file that
# needs no side-by-side module headers. The modular headers below are shipped
# too for plugins that prefer a minimal compile surface.
set(PLUGIN_HEADER
    "${PLUGINSDK_SRC}/include/neverc/Plugin/NevercPluginAPI.h")
set(PLUGIN_MANIFEST "${PLUGINSDK_SRC}/manifest/plugin.json")
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
set(PLUGIN_IR_HEADER
    "${PLUGINSDK_SRC}/../neverc/include/neverc/Plugin/PluginIR.h")
set(PLUGIN_MIR_HEADER
    "${PLUGINSDK_SRC}/../neverc/include/neverc/Plugin/PluginMIR.h")
set(PLUGIN_TARGET_HEADER
    "${PLUGINSDK_SRC}/../neverc/include/neverc/Plugin/PluginTarget.h")
set(PLUGIN_MC_HEADER
    "${PLUGINSDK_SRC}/../neverc/include/neverc/Plugin/PluginMC.h")
set(PLUGIN_OBJECT_HEADER
    "${PLUGINSDK_SRC}/../neverc/include/neverc/Plugin/PluginObject.h")
set(PLUGIN_LINK_HEADER
    "${PLUGINSDK_SRC}/../neverc/include/neverc/Plugin/PluginLink.h")
set(PLUGIN_LTO_HEADER
    "${PLUGINSDK_SRC}/../neverc/include/neverc/Plugin/PluginLTO.h")
set(PLUGIN_DYNCODE_HEADER
    "${PLUGINSDK_SRC}/../neverc/include/neverc/Plugin/PluginDynCode.h")
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
set(PLUGIN_IR_SCHEMA_JSON
    "${PLUGINSDK_SRC}/../neverc/include/neverc/Plugin/Schema/IRSchema.json")
set(PLUGIN_IR_SCHEMA_INC
    "${PLUGINSDK_SRC}/../neverc/include/neverc/Plugin/Schema/PluginIRSchema.inc")
set(PLUGIN_MIR_SCHEMA_JSON
    "${PLUGINSDK_SRC}/../neverc/include/neverc/Plugin/Schema/MIRSchema.json")
set(PLUGIN_MIR_SCHEMA_INC
    "${PLUGINSDK_SRC}/../neverc/include/neverc/Plugin/Schema/PluginMIRSchema.inc")
set(PLUGIN_MC_SCHEMA_JSON
    "${PLUGINSDK_SRC}/../neverc/include/neverc/Plugin/Schema/MCSchema.json")
set(PLUGIN_MC_SCHEMA_INC
    "${PLUGINSDK_SRC}/../neverc/include/neverc/Plugin/Schema/PluginMCSchema.inc")
set(PLUGIN_OBJECT_SCHEMA_JSON
    "${PLUGINSDK_SRC}/../neverc/include/neverc/Plugin/Schema/ObjectSchema.json")
set(PLUGIN_OBJECT_SCHEMA_INC
    "${PLUGINSDK_SRC}/../neverc/include/neverc/Plugin/Schema/PluginObjectSchema.inc")

# All files shipped in the examples/ directory (add new plugins here).
set(EXAMPLE_FILES
    Makefile
    ExamplePlugin.c
    CrtShimPlugin.c
    BenchPlugin.c
    CustomCallConvPlugin.c
    DriverTracePlugin.c
    VirtualHeaderPlugin.c
    ASTRewritePlugin.c
    FunctionPass.c
    MachinePass.c
    MCObserverPlugin.c
    ObjectRewritePlugin.c
    DynCodeTracePlugin.c
    DynCodeEncoderPlugin.c)

list(TRANSFORM EXAMPLE_FILES
     PREPEND "${PLUGINSDK_SRC}/examples/"
     OUTPUT_VARIABLE _example_abs)

set(PYTHON_SDK_ROOT_FILES
    "${PLUGINSDK_SRC}/python/pyproject.toml"
    "${PLUGINSDK_SRC}/python/README.md")
set(PYTHON_SDK_PACKAGE_FILES
    "${PLUGINSDK_SRC}/python/neverc_plugin/__init__.py"
    "${PLUGINSDK_SRC}/python/neverc_plugin/api.py"
    "${PLUGINSDK_SRC}/python/neverc_plugin/phases.py")
set(PYTHON_SDK_DOMAIN_FILES
    "${PLUGINSDK_SRC}/python/neverc_plugin/domains/__init__.py"
    "${PLUGINSDK_SRC}/python/neverc_plugin/domains/driver.py")
set(PYTHON_SDK_EXAMPLE_FILES
    "${PLUGINSDK_SRC}/python/examples/minimal.py"
    "${PLUGINSDK_SRC}/python/examples/driver_trace.py")

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
      "${PLUGIN_IR_HEADER}"
      "${PLUGINSDK_DST}/include/neverc/Plugin/PluginIR.h"
  COMMAND "${CMAKE_COMMAND}" -E copy
      "${PLUGIN_MIR_HEADER}"
      "${PLUGINSDK_DST}/include/neverc/Plugin/PluginMIR.h"
  COMMAND "${CMAKE_COMMAND}" -E copy
      "${PLUGIN_TARGET_HEADER}"
      "${PLUGINSDK_DST}/include/neverc/Plugin/PluginTarget.h"
  COMMAND "${CMAKE_COMMAND}" -E copy
      "${PLUGIN_MC_HEADER}"
      "${PLUGINSDK_DST}/include/neverc/Plugin/PluginMC.h"
  COMMAND "${CMAKE_COMMAND}" -E copy
      "${PLUGIN_OBJECT_HEADER}"
      "${PLUGINSDK_DST}/include/neverc/Plugin/PluginObject.h"
  COMMAND "${CMAKE_COMMAND}" -E copy
      "${PLUGIN_LINK_HEADER}"
      "${PLUGINSDK_DST}/include/neverc/Plugin/PluginLink.h"
  COMMAND "${CMAKE_COMMAND}" -E copy
      "${PLUGIN_LTO_HEADER}"
      "${PLUGINSDK_DST}/include/neverc/Plugin/PluginLTO.h"
  COMMAND "${CMAKE_COMMAND}" -E copy
      "${PLUGIN_DYNCODE_HEADER}"
      "${PLUGINSDK_DST}/include/neverc/Plugin/PluginDynCode.h"
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
  COMMAND "${CMAKE_COMMAND}" -E copy
      "${PLUGIN_IR_SCHEMA_JSON}"
      "${PLUGINSDK_DST}/include/neverc/Plugin/Schema/IRSchema.json"
  COMMAND "${CMAKE_COMMAND}" -E copy
      "${PLUGIN_IR_SCHEMA_INC}"
      "${PLUGINSDK_DST}/include/neverc/Plugin/Schema/PluginIRSchema.inc"
  COMMAND "${CMAKE_COMMAND}" -E copy
      "${PLUGIN_MIR_SCHEMA_JSON}"
      "${PLUGINSDK_DST}/include/neverc/Plugin/Schema/MIRSchema.json"
  COMMAND "${CMAKE_COMMAND}" -E copy
      "${PLUGIN_MIR_SCHEMA_INC}"
      "${PLUGINSDK_DST}/include/neverc/Plugin/Schema/PluginMIRSchema.inc"
  COMMAND "${CMAKE_COMMAND}" -E copy
      "${PLUGIN_MC_SCHEMA_JSON}"
      "${PLUGINSDK_DST}/include/neverc/Plugin/Schema/MCSchema.json"
  COMMAND "${CMAKE_COMMAND}" -E copy
      "${PLUGIN_MC_SCHEMA_INC}"
      "${PLUGINSDK_DST}/include/neverc/Plugin/Schema/PluginMCSchema.inc"
  COMMAND "${CMAKE_COMMAND}" -E copy
      "${PLUGIN_OBJECT_SCHEMA_JSON}"
      "${PLUGINSDK_DST}/include/neverc/Plugin/Schema/ObjectSchema.json"
  COMMAND "${CMAKE_COMMAND}" -E copy
      "${PLUGIN_OBJECT_SCHEMA_INC}"
      "${PLUGINSDK_DST}/include/neverc/Plugin/Schema/PluginObjectSchema.inc"
  COMMAND "${CMAKE_COMMAND}" -E make_directory
      "${PLUGINSDK_DST}/manifest"
  COMMAND "${CMAKE_COMMAND}" -E copy
      "${PLUGIN_MANIFEST}"
      "${PLUGINSDK_DST}/manifest/plugin.json"
  COMMAND "${CMAKE_COMMAND}" -E rm -rf "${PLUGINSDK_DST}/examples"
  COMMAND "${CMAKE_COMMAND}" -E copy_directory
      "${PLUGINSDK_SRC}/examples"
      "${PLUGINSDK_DST}/examples"
  COMMAND "${CMAKE_COMMAND}" -E make_directory
      "${PLUGINSDK_DST}/python/neverc_plugin/domains"
  COMMAND "${CMAKE_COMMAND}" -E make_directory
      "${PLUGINSDK_DST}/python/examples"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      ${PYTHON_SDK_ROOT_FILES}
      "${PLUGINSDK_DST}/python"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      ${PYTHON_SDK_PACKAGE_FILES}
      "${PLUGINSDK_DST}/python/neverc_plugin"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      ${PYTHON_SDK_DOMAIN_FILES}
      "${PLUGINSDK_DST}/python/neverc_plugin/domains"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      ${PYTHON_SDK_EXAMPLE_FILES}
      "${PLUGINSDK_DST}/python/examples"
  COMMAND "${CMAKE_COMMAND}" -E touch "${_stamp}"
  DEPENDS
      "${PLUGIN_HEADER}"
      "${PLUGIN_CORE_HEADER}"
      "${PLUGIN_DRIVER_HEADER}"
      "${PLUGIN_SOURCE_HEADER}"
      "${PLUGIN_PREP_HEADER}"
      "${PLUGIN_AST_HEADER}"
      "${PLUGIN_SEMA_HEADER}"
      "${PLUGIN_IR_HEADER}"
      "${PLUGIN_MIR_HEADER}"
      "${PLUGIN_TARGET_HEADER}"
      "${PLUGIN_MC_HEADER}"
      "${PLUGIN_OBJECT_HEADER}"
      "${PLUGIN_LINK_HEADER}"
      "${PLUGIN_LTO_HEADER}"
      "${PLUGIN_DYNCODE_HEADER}"
      "${PLUGIN_PHASE_SCHEMA_HEADER}"
      "${PLUGIN_PHASE_SCHEMA_INC}"
      "${PLUGIN_PREP_SCHEMA_JSON}"
      "${PLUGIN_PREP_SCHEMA_INC}"
      "${PLUGIN_AST_SCHEMA_JSON}"
      "${PLUGIN_AST_SCHEMA_INC}"
      "${PLUGIN_IR_SCHEMA_JSON}"
      "${PLUGIN_IR_SCHEMA_INC}"
      "${PLUGIN_MIR_SCHEMA_JSON}"
      "${PLUGIN_MIR_SCHEMA_INC}"
      "${PLUGIN_MC_SCHEMA_JSON}"
      "${PLUGIN_MC_SCHEMA_INC}"
      "${PLUGIN_OBJECT_SCHEMA_JSON}"
      "${PLUGIN_OBJECT_SCHEMA_INC}"
      "${PLUGIN_MANIFEST}"
      ${_example_abs}
      ${PYTHON_SDK_ROOT_FILES}
      ${PYTHON_SDK_PACKAGE_FILES}
      ${PYTHON_SDK_DOMAIN_FILES}
      ${PYTHON_SDK_EXAMPLE_FILES}
  COMMENT "Copying pluginsdk to build tree..."
)

function(add_neverc_pluginsdk_example target source)
  get_filename_component(_name "${source}" NAME_WE)
  set(_source "${PLUGINSDK_SRC}/examples/${source}")

  # Build once with the independent C compiler selected by CMake.
  add_library(${target} MODULE "${_source}")
  target_compile_features(${target} PRIVATE c_std_11)
  target_include_directories(${target}
                             PRIVATE "${PLUGINSDK_SRC}/../neverc/include")
  set_target_properties(${target}
                        PROPERTIES PREFIX ""
                                   OUTPUT_NAME "${_name}"
                                   LIBRARY_OUTPUT_DIRECTORY
                                     "${CMAKE_CURRENT_BINARY_DIR}/examples/host")

  # Build the same source with the freshly-built NeverC compiler.
  set(_neverc_output
      "${CMAKE_CURRENT_BINARY_DIR}/examples/neverc/${_name}${CMAKE_SHARED_MODULE_SUFFIX}")
  add_custom_command(
    OUTPUT "${_neverc_output}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${CMAKE_CURRENT_BINARY_DIR}/examples/neverc"
    COMMAND "$<TARGET_FILE:neverc>" --no-default-config -shared
            "-I${PLUGINSDK_SRC}/../neverc/include"
            -o "${_neverc_output}" "${_source}"
    DEPENDS neverc "${_source}" ${_pluginsdk_example_runtime_deps}
    COMMENT "Building ${_name} with NeverC"
    VERBATIM)

  set(NEVERC_PLUGINSDK_HOST_EXAMPLES
      ${NEVERC_PLUGINSDK_HOST_EXAMPLES} ${target}
      PARENT_SCOPE)
  set(NEVERC_PLUGINSDK_NEVERC_EXAMPLES
      ${NEVERC_PLUGINSDK_NEVERC_EXAMPLES} "${_neverc_output}"
      PARENT_SCOPE)
endfunction()

# The NeverC-compiled examples are built with the freshly-built compiler, which
# locates its system headers/libraries from the bundled SDK staged under
# <build>/runtime (getBundledMacOSSysroot() looks for <build>/runtime/macos/<arch>,
# and the Windows toolchain uses the staged MSVC headers). Those copy targets are
# independent ALL-targets, so without an explicit dependency Ninja may compile an
# example before the host sysroot is staged, aborting with "'string.h' file not
# found" / "library not found for -lSystem". Depend on the host sysroot staging
# so the examples stay self-contained and order-independent.
set(_pluginsdk_example_runtime_deps)
if(APPLE)
  if(TARGET neverc-runtime-macos-arm64)
    list(APPEND _pluginsdk_example_runtime_deps neverc-runtime-macos-arm64)
  endif()
elseif(CMAKE_HOST_WIN32)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64")
    if(TARGET neverc-runtime-arm64-msvc)
      list(APPEND _pluginsdk_example_runtime_deps neverc-runtime-arm64-msvc)
    endif()
  elseif(TARGET neverc-runtime-x64-msvc)
    list(APPEND _pluginsdk_example_runtime_deps neverc-runtime-x64-msvc)
  endif()
  if(TARGET neverc-runtime-shared-msvc)
    list(APPEND _pluginsdk_example_runtime_deps neverc-runtime-shared-msvc)
  endif()
elseif(CMAKE_HOST_UNIX)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    if(TARGET neverc-runtime-linux-arm64)
      list(APPEND _pluginsdk_example_runtime_deps neverc-runtime-linux-arm64)
    endif()
  elseif(TARGET neverc-runtime-linux-x64)
    list(APPEND _pluginsdk_example_runtime_deps neverc-runtime-linux-x64)
  endif()
endif()

add_neverc_pluginsdk_example(neverc-plugin-example-driver-trace
                             DriverTracePlugin.c)
add_neverc_pluginsdk_example(neverc-plugin-example-virtual-header
                             VirtualHeaderPlugin.c)
add_neverc_pluginsdk_example(neverc-plugin-example-ast-rewrite
                             ASTRewritePlugin.c)
add_neverc_pluginsdk_example(neverc-plugin-example-ir-overview
                             ExamplePlugin.c)
add_neverc_pluginsdk_example(neverc-plugin-example-crt-shim
                             CrtShimPlugin.c)
add_neverc_pluginsdk_example(neverc-plugin-example-abi-bench
                             BenchPlugin.c)
add_neverc_pluginsdk_example(neverc-plugin-example-custom-callconv
                             CustomCallConvPlugin.c)
add_neverc_pluginsdk_example(neverc-plugin-example-function-pass
                             FunctionPass.c)
add_neverc_pluginsdk_example(neverc-plugin-example-machine-pass
                             MachinePass.c)
add_neverc_pluginsdk_example(neverc-plugin-example-mc-observer
                             MCObserverPlugin.c)
add_neverc_pluginsdk_example(neverc-plugin-example-object-rewrite
                             ObjectRewritePlugin.c)
add_neverc_pluginsdk_example(neverc-plugin-example-dyncode-trace
                             DynCodeTracePlugin.c)
add_neverc_pluginsdk_example(neverc-plugin-example-dyncode-encoder
                             DynCodeEncoderPlugin.c)

add_custom_target(neverc-pluginsdk-examples
                  DEPENDS ${NEVERC_PLUGINSDK_HOST_EXAMPLES}
                          ${NEVERC_PLUGINSDK_NEVERC_EXAMPLES})
set_target_properties(neverc-pluginsdk-examples
                      PROPERTIES FOLDER "NeverC/Plugin SDK")

add_custom_target(neverc-pluginsdk ALL DEPENDS "${_stamp}")
add_dependencies(neverc-pluginsdk neverc-pluginsdk-examples)
if(TARGET nevercPythonPluginPhaseSchema)
  add_dependencies(neverc-pluginsdk nevercPythonPluginPhaseSchema)
endif()

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
        "${PLUGIN_IR_HEADER}"
        "${PLUGIN_MIR_HEADER}"
        "${PLUGIN_TARGET_HEADER}"
        "${PLUGIN_MC_HEADER}"
        "${PLUGIN_OBJECT_HEADER}"
        "${PLUGIN_LINK_HEADER}"
        "${PLUGIN_LTO_HEADER}"
        "${PLUGIN_DYNCODE_HEADER}"
        "${PLUGIN_PHASE_SCHEMA_HEADER}"
        DESTINATION "pluginsdk/include/neverc/Plugin"
        COMPONENT neverc-pluginsdk)
install(FILES "${PLUGIN_PHASE_SCHEMA_INC}"
              "${PLUGIN_PREP_SCHEMA_JSON}"
              "${PLUGIN_PREP_SCHEMA_INC}"
              "${PLUGIN_AST_SCHEMA_JSON}"
              "${PLUGIN_AST_SCHEMA_INC}"
              "${PLUGIN_IR_SCHEMA_JSON}"
              "${PLUGIN_IR_SCHEMA_INC}"
              "${PLUGIN_MIR_SCHEMA_JSON}"
              "${PLUGIN_MIR_SCHEMA_INC}"
              "${PLUGIN_MC_SCHEMA_JSON}"
              "${PLUGIN_MC_SCHEMA_INC}"
              "${PLUGIN_OBJECT_SCHEMA_JSON}"
              "${PLUGIN_OBJECT_SCHEMA_INC}"
        DESTINATION "pluginsdk/include/neverc/Plugin/Schema"
        COMPONENT neverc-pluginsdk)

install(DIRECTORY "${PLUGINSDK_SRC}/schemas/targets"
        DESTINATION "pluginsdk/schemas"
        COMPONENT neverc-pluginsdk
        FILES_MATCHING PATTERN "*.json")

install(FILES "${PLUGIN_MANIFEST}"
        DESTINATION "pluginsdk/manifest"
        COMPONENT neverc-pluginsdk)

install(FILES ${_example_abs}
        DESTINATION "pluginsdk/examples"
        COMPONENT neverc-pluginsdk)

install(FILES ${PYTHON_SDK_ROOT_FILES}
        DESTINATION "pluginsdk/python"
        COMPONENT neverc-pluginsdk)
install(FILES ${PYTHON_SDK_PACKAGE_FILES}
        DESTINATION "pluginsdk/python/neverc_plugin"
        COMPONENT neverc-pluginsdk)
install(FILES ${PYTHON_SDK_DOMAIN_FILES}
        DESTINATION "pluginsdk/python/neverc_plugin/domains"
        COMPONENT neverc-pluginsdk)
install(FILES ${PYTHON_SDK_EXAMPLE_FILES}
        DESTINATION "pluginsdk/python/examples"
        COMPONENT neverc-pluginsdk)

# ---------------------------------------------------------------------------
# CMake package config, pkg-config and scaffolding templates
# ---------------------------------------------------------------------------
include(CMakePackageConfigHelpers)

# Install-tree relative locations resolved by the generated config.
set(NEVERC_PLUGINSDK_INCLUDE_INSTALL_DIR "pluginsdk/include")
set(NEVERC_PLUGINSDK_MANIFEST_INSTALL_FILE "pluginsdk/manifest/plugin.json")

set(_pluginsdk_cmake_build "${CMAKE_CURRENT_BINARY_DIR}/cmake")
configure_package_config_file(
  "${PLUGINSDK_SRC}/cmake/NevercPluginSDKConfig.cmake.in"
  "${_pluginsdk_cmake_build}/NevercPluginSDKConfig.cmake"
  INSTALL_DESTINATION "pluginsdk/cmake"
  PATH_VARS NEVERC_PLUGINSDK_INCLUDE_INSTALL_DIR
            NEVERC_PLUGINSDK_MANIFEST_INSTALL_FILE)

# Honor the hand-maintained SameMajorVersion template while keeping the
# helper's argument validation.
configure_file(
  "${PLUGINSDK_SRC}/cmake/NevercPluginSDKConfigVersion.cmake.in"
  "${_pluginsdk_cmake_build}/NevercPluginSDKConfigVersion.cmake"
  @ONLY)

configure_file(
  "${PLUGINSDK_SRC}/pkgconfig/neverc-plugin.pc.in"
  "${CMAKE_CURRENT_BINARY_DIR}/pkgconfig/neverc-plugin.pc"
  @ONLY)

install(FILES
        "${_pluginsdk_cmake_build}/NevercPluginSDKConfig.cmake"
        "${_pluginsdk_cmake_build}/NevercPluginSDKConfigVersion.cmake"
        DESTINATION "pluginsdk/cmake"
        COMPONENT neverc-pluginsdk)

install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/pkgconfig/neverc-plugin.pc"
        DESTINATION "pluginsdk/pkgconfig"
        COMPONENT neverc-pluginsdk)

install(DIRECTORY "${PLUGINSDK_SRC}/templates"
        DESTINATION "pluginsdk"
        COMPONENT neverc-pluginsdk)

if(NOT LLVM_ENABLE_IDE)
  add_llvm_install_targets(install-neverc-pluginsdk
                           DEPENDS neverc-pluginsdk
                           COMPONENT neverc-pluginsdk)
endif()
