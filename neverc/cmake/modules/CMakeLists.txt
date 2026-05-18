include(GNUInstallPackageDir)
include(ExtendPath)
include(LLVMDistributionSupport)
include(FindPrefixFromConfig)

# Generate a list of CMake library targets so that other CMake projects can
# link against them. LLVM calls its version of this file LLVMExports.cmake, but
# the usual CMake convention seems to be ${Project}Targets.cmake.
set(NEVERC_INSTALL_PACKAGE_DIR "${CMAKE_INSTALL_PACKAGEDIR}/neverc" CACHE STRING
  "Path for CMake subdirectory for NeverC (defaults to '${CMAKE_INSTALL_PACKAGEDIR}/neverc')")
# CMAKE_INSTALL_PACKAGEDIR might be absolute, so don't reuse below.
set(neverc_cmake_builddir "${CMAKE_BINARY_DIR}/lib${LLVM_LIBDIR_SUFFIX}/cmake/neverc")

# Keep this in sync with llvm/cmake/CMakeLists.txt!
set(LLVM_INSTALL_PACKAGE_DIR "${CMAKE_INSTALL_PACKAGEDIR}/llvm" CACHE STRING
  "Path for CMake subdirectory for LLVM (defaults to '${CMAKE_INSTALL_PACKAGEDIR}/llvm')")
# CMAKE_INSTALL_PACKAGEDIR might be absolute, so don't reuse below.
string(REPLACE "${CMAKE_CFG_INTDIR}" "." llvm_cmake_builddir "${LLVM_LIBRARY_DIR}")
set(llvm_cmake_builddir "${llvm_cmake_builddir}/cmake/llvm")

get_property(NEVERC_EXPORTS GLOBAL PROPERTY NEVERC_EXPORTS)
export(TARGETS ${NEVERC_EXPORTS} FILE ${neverc_cmake_builddir}/NeverCTargets.cmake)

# Generate NeverCConfig.cmake for the build tree.
set(NEVERC_CONFIG_CMAKE_DIR "${neverc_cmake_builddir}")
set(NEVERC_CONFIG_LLVM_CMAKE_DIR "${llvm_cmake_builddir}")
set(NEVERC_CONFIG_INCLUDE_EXPORTS "include(\"${neverc_cmake_builddir}/NeverCTargets.cmake\")")
set(NEVERC_CONFIG_INCLUDE_DIRS
  "${NEVERC_SOURCE_DIR}/include"
  "${NEVERC_BINARY_DIR}/include"
  )
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/NeverCConfig.cmake.in
  ${neverc_cmake_builddir}/NeverCConfig.cmake
  @ONLY)
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/NeverCConfigVersion.cmake.in
  ${neverc_cmake_builddir}/NeverCConfigVersion.cmake
  @ONLY)
set(NEVERC_CONFIG_CMAKE_DIR)
set(NEVERC_CONFIG_LLVM_CMAKE_DIR)

# For compatibility with projects that include(NeverCConfig)
# via CMAKE_MODULE_PATH, place API modules next to it.
# Copy without source permissions because the source could be read-only,
# but we need to write into the copied folder.
file(COPY .
  DESTINATION ${neverc_cmake_builddir}
  NO_SOURCE_PERMISSIONS
  FILES_MATCHING PATTERN *.cmake
  PATTERN CMakeFiles EXCLUDE
  )

# Generate NeverCConfig.cmake for the install tree.
find_prefix_from_config(NEVERC_CONFIG_CODE NEVERC_INSTALL_PREFIX "${NEVERC_INSTALL_PACKAGE_DIR}")
extend_path(NEVERC_CONFIG_CMAKE_DIR "\${NEVERC_INSTALL_PREFIX}" "${NEVERC_INSTALL_PACKAGE_DIR}")
extend_path(NEVERC_CONFIG_LLVM_CMAKE_DIR "\${NEVERC_INSTALL_PREFIX}" "${LLVM_INSTALL_PACKAGE_DIR}")
get_config_exports_includes(NeverC NEVERC_CONFIG_INCLUDE_EXPORTS)
extend_path(base_includedir "\${NEVERC_INSTALL_PREFIX}" "${CMAKE_INSTALL_INCLUDEDIR}")
set(NEVERC_CONFIG_INCLUDE_DIRS
  "${base_includedir}"
  )
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/NeverCConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/NeverCConfig.cmake
  @ONLY)
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/NeverCConfigVersion.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/NeverCConfigVersion.cmake
  @ONLY)
set(NEVERC_CONFIG_CODE)
set(NEVERC_CONFIG_CMAKE_DIR)

if (NOT LLVM_INSTALL_TOOLCHAIN_ONLY)
  install_distribution_exports(NeverC)

  install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/NeverCConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/NeverCConfigVersion.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/AddNeverC.cmake
    DESTINATION ${NEVERC_INSTALL_PACKAGE_DIR}
    COMPONENT neverc-cmake-exports)

  if(NOT LLVM_ENABLE_IDE)
    # Add a dummy target so this can be used with LLVM_DISTRIBUTION_COMPONENTS
    add_custom_target(neverc-cmake-exports)
    add_llvm_install_targets(install-neverc-cmake-exports
                             COMPONENT neverc-cmake-exports)
  endif()
endif()
