# NeverC Standard Library — bitcode embedding build
#
# Each .c is compiled to LLVM bitcode, merged into a single .bc blob,
# and converted to an embedded C header for linking into the compiler.
#
# Headers are installed to the resource dir.  Sources (.c) are used for
# building bitcode and always shipped alongside headers for source-level
# debugging (DWARF paths map to runtime/std/src/ via -fdebug-prefix-map).
# Runtime tests live in tests/neverc/std/ and are built as neverc-std-tests.

include(GetNeverCResourceDir)
get_neverc_resource_dir(std_hdr_install_dir SUBDIR include)

# Install std headers alongside builtin headers so #include <neverc/std/math.h> works.
install(DIRECTORY include/ DESTINATION ${std_hdr_install_dir} COMPONENT neverc-std)

# During development builds, copy std headers to the build output's resource dir
# so `build-neverc/bin/neverc` can find them without installing.
get_neverc_resource_dir(std_hdr_build_dir PREFIX ${LLVM_LIBRARY_OUTPUT_INTDIR}/.. SUBDIR include)
add_custom_target(neverc-std-headers ALL
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${std_hdr_build_dir}
  COMMENT "Syncing NeverC std headers to build output...")
set_target_properties(neverc-std-headers PROPERTIES FOLDER "Misc")
if(TARGET neverc-resource-headers)
  add_dependencies(neverc-resource-headers neverc-std-headers)
endif()

# Manifest for the compiler's symbol→bitcode lookup.
install(FILES manifest.json DESTINATION std COMPONENT neverc-std)

# Install std source files for source-level debugging.
# Debuggers use the DWARF paths mapped by -fdebug-prefix-map in
# build_std_bitcode.py to locate these under runtime/std/src/.
install(DIRECTORY src/ DESTINATION runtime/std/src
        COMPONENT neverc-std
        FILES_MATCHING PATTERN "*.c" PATTERN "*.h")

# Copy std sources to the build tree so dev builds support source-level
# debugging without a full install.
set(_std_src_build_dst "${LLVM_LIBRARY_OUTPUT_INTDIR}/../runtime/std/src")
add_custom_target(neverc-std-sources ALL
  COMMAND ${CMAKE_COMMAND} -E rm -rf "${_std_src_build_dst}"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${_std_src_build_dst}
  COMMENT "Syncing NeverC std sources to build output..."
)
set_target_properties(neverc-std-sources PROPERTIES FOLDER "Misc")
